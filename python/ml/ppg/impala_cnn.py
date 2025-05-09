import math

import torch as th
from torch import nn
from torch.nn import functional as F

from . import torch_util as tu
from gym3.types import Real, TensorType

REAL = Real()


class Encoder(nn.Module):
    """
    Takes in seq of observations and outputs sequence of codes

    Encoders can be stateful, meaning that you pass in one observation at a
    time and update the state, which is a separate object. (This object
    doesn't store any state except parameters)
    """

    def __init__(self, obtype, codetype):
        super().__init__()
        self.obtype = obtype
        self.codetype = codetype

    def initial_state(self, batchsize):
        raise NotImplementedError

    def empty_state(self):
        return None

    def stateless_forward(self, obs):
        """
        inputs:
            obs: array or dict, all with preshape (B, T)
        returns:
            codes: array or dict, all with preshape (B, T)
        """
        code, _state = self(obs, None, self.empty_state())
        return code

    def forward(self, obs, first, state_in):
        """
        inputs:
            obs: array or dict, all with preshape (B, T)
            first: float array shape (B, T)
            state_in: array or dict, all with preshape (B,)
        returns:
            codes: array or dict
            state_out: array or dict
        """
        raise NotImplementedError


class CnnBasicBlock(nn.Module):
    """
    Residual basic block (without batchnorm), as in ImpalaCNN
    Preserves channel number and shape
    """

    def __init__(self, inchan, scale=1, batch_norm=False):
        super().__init__()
        self.inchan = inchan
        self.batch_norm = batch_norm
        s = math.sqrt(scale)
        self.conv0 = tu.NormedConv2d(self.inchan, self.inchan, 3, padding=1, scale=s)
        self.conv1 = tu.NormedConv2d(self.inchan, self.inchan, 3, padding=1, scale=s)
        if self.batch_norm:
            self.bn0 = nn.BatchNorm2d(self.inchan)
            self.bn1 = nn.BatchNorm2d(self.inchan)

    def residual(self, x):
        # inplace should be False for the first relu, so that it does not change the input,
        # which will be used for skip connection.
        # getattr is for backwards compatibility with loaded models
        if getattr(self, "batch_norm", False):
            x = self.bn0(x)
        x = F.relu(x, inplace=False)
        x = self.conv0(x)
        if getattr(self, "batch_norm", False):
            x = self.bn1(x)
        x = F.relu(x, inplace=True)
        x = self.conv1(x)
        return x

    def forward(self, x):
        return x + self.residual(x)


class CnnDownStack(nn.Module):
    """
    Downsampling stack from Impala CNN
    """

    def __init__(self, inchan, nblock, outchan, scale=1, pool=True, **kwargs):
        super().__init__()
        self.inchan = inchan
        self.outchan = outchan
        self.pool = pool
        self.firstconv = tu.NormedConv2d(inchan, outchan, 3, padding=1)
        s = scale / math.sqrt(nblock)
        self.blocks = nn.ModuleList(
            [CnnBasicBlock(outchan, scale=s, **kwargs) for _ in range(nblock)]
        )

    def forward(self, x):
        x = self.firstconv(x)
        if getattr(self, "pool", True):
            x = F.max_pool2d(x, kernel_size=3, stride=2, padding=1)
        for block in self.blocks:
            x = block(x)
        return x

    def output_shape(self, inshape):
        c, h, w = inshape
        assert c == self.inchan
        if getattr(self, "pool", True):
            return (self.outchan, (h + 1) // 2, (w + 1) // 2)
        else:
            return (self.outchan, h, w)


class ImpalaCNN(nn.Module):
    name = "ImpalaCNN"  # put it here to preserve pickle compat

    def __init__(
        self, inshape, chans, outsize, scale_ob, nblock, final_relu=True, **kwargs
    ):
        super().__init__()
        self.scale_ob = scale_ob
        h, w, c = inshape
        curshape = (c, h, w)
        s = 1 / math.sqrt(len(chans))  # per stack scale
        self.stacks = nn.ModuleList()
        for outchan in chans:
            stack = CnnDownStack(
                curshape[0], nblock=nblock, outchan=outchan, scale=s, **kwargs
            )
            self.stacks.append(stack)
            curshape = stack.output_shape(curshape)
        self.dense = tu.NormedLinear(tu.intprod(curshape), outsize, scale=1.4)
        self.outsize = outsize
        self.final_relu = final_relu

    def forward(self, x):
        x = x.to(dtype=th.float32) / self.scale_ob

        b, t = x.shape[:-3]
        x = x.reshape(b * t, *x.shape[-3:])
        x = tu.transpose(x, "bhwc", "bchw")
        x = tu.sequential(self.stacks, x, diag_name=self.name)
        x = x.reshape(b, t, *x.shape[1:])
        x = tu.flatten_image(x)
        x = th.relu(x)
        x = self.dense(x)
        if self.final_relu:
            x = th.relu(x)
        return x


class ImpalaEncoder(Encoder):
    def __init__(
        self,
        inshape,
        outsize=256,
        chans=(16, 32, 32),
        scale_ob=255.0,
        nblock=2,
        **kwargs
    ):
        codetype = TensorType(eltype=REAL, shape=(outsize,))
        obtype = TensorType(eltype=REAL, shape=inshape)
        super().__init__(codetype=codetype, obtype=obtype)
        self.cnn = ImpalaCNN(
            inshape=inshape,
            chans=chans,
            scale_ob=scale_ob,
            nblock=nblock,
            outsize=outsize,
            **kwargs
        )

    def forward(self, x, first, state_in):
        x = self.cnn(x)
        return x, state_in

    def initial_state(self, batchsize):
        return tu.zeros(batchsize, 0)


class BatchNormSkippingNN(nn.Module):
    def __init__(self, h):
        super().__init__()
        layers = []
        layers.append(nn.BatchNorm1d(h))
        self.net = nn.Sequential(*layers)

    def forward(self, x):
        b = x.shape[0]
        if b == 1:
            return x

        return self.net(x)


class FullyConnectedNN(nn.Module):
    """
    A simple MLP that takes in (B,T,features) and outputs (B,T,outsize).

    This replaces the ImpalaCNN for non-image inputs.
    """

    def __init__(self, in_size, out_size=256, hidden_sizes=(256, 256), scale_ob=1.0):
        super().__init__()
        self.scale_ob = scale_ob
        layers = []
        prev_size = in_size
        for h in hidden_sizes:
            layers.append(tu.NormedLinear(prev_size, h, scale=1.0))
            layers.append(nn.LayerNorm(h))
            layers.append(nn.ReLU())
            prev_size = h
        layers.append(tu.NormedLinear(prev_size, out_size, scale=1.0))
        self.net = nn.Sequential(*layers)
        self.outsize = out_size

    def forward(self, x):
        # x shape: (B,T,features)
        x = x.to(dtype=th.float32) / self.scale_ob
        B, T = x.shape[:2]
        x = x.reshape(B * T, -1)
        x = self.net(x)
        x = x.reshape(B, T, self.outsize)
        return x


class FullyConnectedEncoder(Encoder):
    def __init__(
        self, inshape, outsize=256, hidden_sizes=(256, 256), scale_ob=1.0, **kwargs
    ):
        """
        inshape: tuple of input dimensions, e.g. (features,)
        outsize: size of the output embedding
        hidden_sizes: tuple defining the hidden layer sizes of the MLP
        scale_ob: scaling factor for inputs, default 1.0 if not image-based
        """
        codetype = TensorType(eltype=REAL, shape=(outsize,))
        obtype = TensorType(eltype=REAL, shape=inshape)
        super().__init__(obtype=obtype, codetype=codetype)

        # Compute the input dimension from inshape
        input_dim = 1
        for d in inshape:
            input_dim *= d

        self.mlp = FullyConnectedNN(
            in_size=input_dim,
            out_size=outsize,
            hidden_sizes=hidden_sizes,
            scale_ob=scale_ob,
        )

    def forward(self, x, first, state_in):
        # x shape: (B,T, *inshape)
        # Flatten any extra dimensions beyond (B,T) if necessary
        # If inshape = (features,), x already has shape (B,T,features)
        return self.mlp(x), state_in

    def initial_state(self, batchsize):
        return tu.zeros(batchsize, 0)

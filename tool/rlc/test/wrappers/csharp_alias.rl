# RUN: split-file %s %t

# RUN: rlc %t/source.rl -o %t/Lib%sharedext -i %stdlib --shared
# RUN: rlc %t/source.rl -o %t/csharp.cs -i %stdlib --c-sharp

# RUN: mcs -out:%t/executable.exe %t/csharp.cs %t/main.cs -unsafe
# RUN: mono %t/executable.exe
# REQUIRES: has_mono

#--- source.rl
import collections.vector 
using ThisOne = Int | Float

#--- main.cs
using System;
using System.IO;
using System.Reflection;
class Tester {
    public static int Main() {
        RLCNative.setup(Path.GetDirectoryName(Assembly.GetEntryAssembly().Location) + "/Lib" + RLCNative.SharedLibExtension);
        ThisOne pair = new ThisOne();
        return 0; 
    }
}


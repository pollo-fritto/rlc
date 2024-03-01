# RUN: rlc %s -o %t -i %stdlib 
# RUN: %t

import action

act example() -> Name:
    act first(Int x)
    frm to_return = x
    act second(Bool x, Float y)

fun main() -> Int:
    let any_action : AnyNameAction

    let actual_action : NameFirst
    actual_action.x = 1
    any_action.content = actual_action

    let frame = example()
    apply(any_action.content, frame)
    if frame.to_return == 1:
       return 0 
    return 1

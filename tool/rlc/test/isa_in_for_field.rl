# RUN: rlc %s -o %t -i %stdlib 
# RUN: %t

trait<T> Trait:
    fun function(T a) -> Bool

fun<T> template(T to_add) -> Bool:
    for field of to_add:
        if field is Trait:
            return field.function()
    return false

ent Entity:
    Int x 

ent Outer:
    Entity x 

fun function(Entity a) -> Bool:
    return true

fun main() -> Int:
    let entity : Outer 
    if template(entity):
        return 0
    return 1

"""Pin kinds for UE's native material execution expressions."""


EXECUTION_INPUTS = frozenset(("ExecEnd", "IfThenElse", "SetLocal", "ForLoop", "WhileLoop"))
EXECUTION_OUTPUTS = dict(ExecBegin=("Exec",), IfThenElse=("Then", "Else"),
                         SetLocal=("Exec",), ForLoop=("LoopBody", "Completed"),
                         WhileLoop=("LoopBody", "Completed"))


def is_execution_pin(declaration, name, direction):
    if declaration is None:
        return False
    declared = declaration.type_name
    if "." in declared and not declared.startswith("/Script/Engine."):
        return False
    native = declared.rsplit(".", 1)[-1].removeprefix("MaterialExpression")
    if direction == "in":
        return native in EXECUTION_INPUTS and str(name).casefold() == "execute"
    outputs = EXECUTION_OUTPUTS.get(native, ())
    if name is None:
        return len(outputs) == 1
    return str(name).casefold() in tuple(output.casefold() for output in outputs)

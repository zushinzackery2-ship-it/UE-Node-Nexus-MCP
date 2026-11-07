"""Generate explicit edit intents, independently of the product's diff logic."""

import random
import re


def generate(seed, steps):
    randomizer = random.Random(seed)
    actions = []
    for index in range(steps):
        actions.append(dict(index=index, value=randomizer.choice([0, 0.125, 0.25, 0.5, 1]),
                            two_sided=randomizer.choice([False, True]),
                            output=randomizer.choice(["Valid", "Accepted", "Result"]),
                            output_type=randomizer.choice(["bool", "int"])))
    return actions


def documents(root, schema, action):
    header = lambda name, kind: f"nexus: 1\nasset: {root}/{name}\nclass: {kind}\nschema: {schema}\n"
    material = header("M_Sequence", "Material") + (
        f"[asset]\nTwoSided = {str(action['two_sided']).lower()}\n"
        f"[graph]\nc : Constant(R={action['value']})\nc -> out.EmissiveColor\n")
    provider = header("BFL_Sequence", "Blueprint") + (
        "[asset]\nParentClass = /Script/Engine.BlueprintFunctionLibrary\nBlueprintType = function_library\n"
        f"[function Query() -> ({action['output']}: {action['output_type']} = {'true' if action['output_type'] == 'bool' else '37'})]\nentry.then -> result.execute\n")
    caller = header("BP_Sequence", "Blueprint") + (
        "[asset]\nParentClass = /Script/Engine.Actor\n"
        "[defaults]\nPrimaryActorTick = (bCanEverTick=False,bStartWithTickEnabled=False)\n"
        f"[function Exercise() -> (Value: {action['output_type']})]\n"
        f"call : CallFunction({root}/BFL_Sequence.BFL_Sequence_C.Query)\n"
        "entry.then -> call.execute\ncall.then -> result.execute\n"
        f"call.{action['output']} -> result.Value\n")
    return {"M_Sequence.mat.nexus": material, "BFL_Sequence.bp.nexus": provider, "BP_Sequence.bp.nexus": caller}


def edit_existing(name, text, action):
    if name.endswith(".mat.nexus"):
        text = re.sub(r"(?m)^TwoSided\s*=.*\n", "", text)
        text = text.replace("[asset]\n", f"[asset]\nTwoSided = {str(action['two_sided']).lower()}\n")
        text, count = re.subn(r"(?m)(^c\s*:\s*Constant)(?:\([^)]*\))?", rf"\g<1>(R={action['value']})", text)
        if count != 1:
            raise ValueError("material fixture lost its authored constant")
        return text
    function = "Query" if name.startswith("BFL_") else "Exercise"
    output = action["output"] if function == "Query" else "Value"
    default = (" = true" if action["output_type"] == "bool" else " = 37") if function == "Query" else ""
    text = re.sub(rf"(\[function {function}\(\) -> \()[^)]*(\))",
                  rf"\g<1>{output}: {action['output_type']}{default}\2", text)
    if function == "Exercise":
        text = re.sub(r"(call\.)\w+( -> result.Value)", rf"\g<1>{action['output']}\2", text)
    return text

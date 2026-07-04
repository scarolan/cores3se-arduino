Import("env")
import os

envfile = os.path.join(env.subst("$PROJECT_DIR"), ".env")
if os.path.isfile(envfile):
    with open(envfile) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                env.Append(CPPDEFINES=[(key.strip(), env.StringifyMacro(value.strip()))])

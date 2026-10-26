# Simulator only: GCC 15 defaults C to C23, where `bool` is a keyword and
# QRCode's `typedef uint8_t bool` fails. Build C sources as gnu17.
Import("env")

for e in (env, DefaultEnvironment()):
    e.Append(CFLAGS=["-std=gnu17"])

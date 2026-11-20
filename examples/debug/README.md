# Debugging example

A small program and a VS Code setup for debugging it on the simulator.

## In VS Code

1. Install the **LLDB DAP** extension (`llvm-vs-code-extensions.lldb-dap`).
2. Open this folder and press F5.

`.vscode/tasks.json` builds `main.c` with debug information and starts the
simulator's debug server; `.vscode/launch.json` attaches the toolchain's
own LLDB to it and runs to `main`. The device, the optimization level and
the toolchain's location are in `.vscode/settings.json`. For a project of
your own, copy the `.vscode` folder and set `pic16f1.toolchain` to where
this repository is.

## From a terminal

```sh
../../toolchain/bin/pic16f1-cc -g -O1 -mcpu=pic16f1937 -o build/main.hex main.c
../../toolchain/bin/pic16f1-sim gdb --port 3333 build/main.elf &
../../toolchain/bin/pic16f1-lldb build/main.elf -o "gdb-remote 3333"
```

Things to try: `breakpoint set --name scale`, `continue`, `bt`,
`frame variable`, `up`, `finish`, `next`, `p total + 1`,
`watchpoint set variable total`, `register read --all`.

`sum_to` is recursive, so its calls have stack frames; the other functions
keep their variables at fixed addresses. Both show in every frame of the
call stack.

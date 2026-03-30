target extended-remote :3333
load
set print asm-demangle on
set print pretty on
set style sources off
monitor reset halt
break main
continue
step

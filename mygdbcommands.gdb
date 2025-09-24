set pagination off
set architecture aarch64
add-symbol-file /home/ahmed/WorkDir/reactos_arm64/output-test-clang/ntoskrnl/ntoskrnl.exe 
target remote localhost:1234
hbreak *0xFFFF800042389328
hbreak *0xFFFF80004238D328
hbreak *0xFFFF800042160800
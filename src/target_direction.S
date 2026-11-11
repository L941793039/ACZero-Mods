.intel_syntax noprefix
.text
.globl targetDirectionBridge
targetDirectionBridge:
  # Entered by JMP replacing b056d CALL. Native RSP is 16-byte aligned.
  # Pass the fifth (texture) argument and shadow space to the C++ packet
  # wrapper. Its ABI preserves the same nonvolatile state as native 1fe080.
  sub rsp, 0x30
  mov rax, QWORD PTR [rsp + 0x50]
  mov QWORD PTR [rsp + 0x20], rax
  call targetDirectionPacket
  add rsp, 0x30
  jmp QWORD PTR [rip + targetDirectionReturn]
.att_syntax prefix

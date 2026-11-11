.intel_syntax noprefix
.text
.globl targetRangeBridge
targetRangeBridge:
  # Replace exactly the native 8-byte VMOVSS xmm8,[constant] at b9b01.
  # Same VEX scalar-load semantics; no flags or general registers touched.
  vmovss xmm8, DWORD PTR [rip + targetRangeForDraw]
  jmp QWORD PTR [rip + targetRangeReturn]
.att_syntax prefix

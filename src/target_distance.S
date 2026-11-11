.intel_syntax noprefix
.text
.globl targetDistanceBridge
targetDistanceBridge:
  # Replace only b9bf9's 9-byte indexed VMOVSS. Keep every register/flag
  # except its intended xmm6 result, including full scratch ymm0 contents.
  pushfq
  cmp DWORD PTR [rip + targetDrawDistanceOn], 0
  je .Lcached_distance
  lea rsp, [rsp - 32]
  vmovdqu YMMWORD PTR [rsp], ymm0
  vmovups xmm6, XMMWORD PTR [rbx + 0x258]
  vsubps xmm6, xmm6, XMMWORD PTR [rip + targetDrawOrigin]
  vmulps xmm6, xmm6, xmm6
  vshufps xmm0, xmm6, xmm6, 0x55
  vaddss xmm6, xmm6, xmm0
  vshufps xmm0, xmm6, xmm6, 0xaa
  vaddss xmm6, xmm6, xmm0
  vsqrtss xmm6, xmm6, xmm6
  vxorps xmm0, xmm0, xmm0
  vmovss xmm6, xmm0, xmm6
  vmovdqu ymm0, YMMWORD PTR [rsp]
  lea rsp, [rsp + 32]
  popfq
  jmp QWORD PTR [rip + targetDistanceReturn]
.Lcached_distance:
  vmovss xmm6, DWORD PTR [rbx + rax*4 + 0x20c]
  popfq
  jmp QWORD PTR [rip + targetDistanceReturn]
.att_syntax prefix

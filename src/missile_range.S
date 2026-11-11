.intel_syntax noprefix
.text
.globl normalFlightRangeBridge
normalFlightRangeBridge:
  vmulss xmm1, xmm0, DWORD PTR [rip + missileRangeNativeScale]
  pushfq
  push rax
  push rcx
  push rdx
  push rbx
  push rbp
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  sub rsp, 0x240
  vmovdqu [rsp + 32], ymm0
  vmovdqu [rsp + 64], ymm1
  vmovdqu [rsp + 96], ymm2
  vmovdqu [rsp + 128], ymm3
  vmovdqu [rsp + 160], ymm4
  vmovdqu [rsp + 192], ymm5
  vmovdqu [rsp + 224], ymm6
  vmovdqu [rsp + 256], ymm7
  vmovdqu [rsp + 288], ymm8
  vmovdqu [rsp + 320], ymm9
  vmovdqu [rsp + 352], ymm10
  vmovdqu [rsp + 384], ymm11
  vmovdqu [rsp + 416], ymm12
  vmovdqu [rsp + 448], ymm13
  vmovdqu [rsp + 480], ymm14
  vmovdqu [rsp + 512], ymm15
  lea rcx, [rbx - 0x28]
  mov r8d, 0
  vzeroupper
  call missileFlightLimit
  vmovss DWORD PTR [rsp + 64], xmm0
  vmovdqu ymm0, [rsp + 32]
  vmovdqu ymm1, [rsp + 64]
  vmovdqu ymm2, [rsp + 96]
  vmovdqu ymm3, [rsp + 128]
  vmovdqu ymm4, [rsp + 160]
  vmovdqu ymm5, [rsp + 192]
  vmovdqu ymm6, [rsp + 224]
  vmovdqu ymm7, [rsp + 256]
  vmovdqu ymm8, [rsp + 288]
  vmovdqu ymm9, [rsp + 320]
  vmovdqu ymm10, [rsp + 352]
  vmovdqu ymm11, [rsp + 384]
  vmovdqu ymm12, [rsp + 416]
  vmovdqu ymm13, [rsp + 448]
  vmovdqu ymm14, [rsp + 480]
  vmovdqu ymm15, [rsp + 512]
  add rsp, 0x240
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rbx
  pop rdx
  pop rcx
  pop rax
  popfq
  jmp QWORD PTR [rip + normalFlightRangeReturn]
.globl xagmFlightRangeBridge
xagmFlightRangeBridge:
  vmulss xmm1, xmm6, DWORD PTR [rax + 0x14]
  pushfq
  push rax
  push rcx
  push rdx
  push rbx
  push rbp
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  sub rsp, 0x240
  vmovdqu [rsp + 32], ymm0
  vmovdqu [rsp + 64], ymm1
  vmovdqu [rsp + 96], ymm2
  vmovdqu [rsp + 128], ymm3
  vmovdqu [rsp + 160], ymm4
  vmovdqu [rsp + 192], ymm5
  vmovdqu [rsp + 224], ymm6
  vmovdqu [rsp + 256], ymm7
  vmovdqu [rsp + 288], ymm8
  vmovdqu [rsp + 320], ymm9
  vmovdqu [rsp + 352], ymm10
  vmovdqu [rsp + 384], ymm11
  vmovdqu [rsp + 416], ymm12
  vmovdqu [rsp + 448], ymm13
  vmovdqu [rsp + 480], ymm14
  vmovdqu [rsp + 512], ymm15
  lea rcx, [rbx - 0x28]
  mov r8d, 7
  vzeroupper
  call missileFlightLimit
  vmovss DWORD PTR [rsp + 64], xmm0
  vmovdqu ymm0, [rsp + 32]
  vmovdqu ymm1, [rsp + 64]
  vmovdqu ymm2, [rsp + 96]
  vmovdqu ymm3, [rsp + 128]
  vmovdqu ymm4, [rsp + 160]
  vmovdqu ymm5, [rsp + 192]
  vmovdqu ymm6, [rsp + 224]
  vmovdqu ymm7, [rsp + 256]
  vmovdqu ymm8, [rsp + 288]
  vmovdqu ymm9, [rsp + 320]
  vmovdqu ymm10, [rsp + 352]
  vmovdqu ymm11, [rsp + 384]
  vmovdqu ymm12, [rsp + 416]
  vmovdqu ymm13, [rsp + 448]
  vmovdqu ymm14, [rsp + 480]
  vmovdqu ymm15, [rsp + 512]
  add rsp, 0x240
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rbx
  pop rdx
  pop rcx
  pop rax
  popfq
  jmp QWORD PTR [rip + xagmFlightRangeReturn]
.globl xmaaFlightRangeBridge
xmaaFlightRangeBridge:
  vmulss xmm1, xmm0, xmm9
  pushfq
  push rax
  push rcx
  push rdx
  push rbx
  push rbp
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  sub rsp, 0x240
  vmovdqu [rsp + 32], ymm0
  vmovdqu [rsp + 64], ymm1
  vmovdqu [rsp + 96], ymm2
  vmovdqu [rsp + 128], ymm3
  vmovdqu [rsp + 160], ymm4
  vmovdqu [rsp + 192], ymm5
  vmovdqu [rsp + 224], ymm6
  vmovdqu [rsp + 256], ymm7
  vmovdqu [rsp + 288], ymm8
  vmovdqu [rsp + 320], ymm9
  vmovdqu [rsp + 352], ymm10
  vmovdqu [rsp + 384], ymm11
  vmovdqu [rsp + 416], ymm12
  vmovdqu [rsp + 448], ymm13
  vmovdqu [rsp + 480], ymm14
  vmovdqu [rsp + 512], ymm15
  lea rcx, [rbx - 0x28]
  mov r8d, 10
  vzeroupper
  call missileFlightLimit
  vmovss DWORD PTR [rsp + 64], xmm0
  vmovdqu ymm0, [rsp + 32]
  vmovdqu ymm1, [rsp + 64]
  vmovdqu ymm2, [rsp + 96]
  vmovdqu ymm3, [rsp + 128]
  vmovdqu ymm4, [rsp + 160]
  vmovdqu ymm5, [rsp + 192]
  vmovdqu ymm6, [rsp + 224]
  vmovdqu ymm7, [rsp + 256]
  vmovdqu ymm8, [rsp + 288]
  vmovdqu ymm9, [rsp + 320]
  vmovdqu ymm10, [rsp + 352]
  vmovdqu ymm11, [rsp + 384]
  vmovdqu ymm12, [rsp + 416]
  vmovdqu ymm13, [rsp + 448]
  vmovdqu ymm14, [rsp + 480]
  vmovdqu ymm15, [rsp + 512]
  add rsp, 0x240
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rbx
  pop rdx
  pop rcx
  pop rax
  popfq
  jmp QWORD PTR [rip + xmaaFlightRangeReturn]
.globl qaamFlightRangeBridge
qaamFlightRangeBridge:
  vmulss xmm1, xmm8, DWORD PTR [rax + 0x14]
  pushfq
  push rax
  push rcx
  push rdx
  push rbx
  push rbp
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  sub rsp, 0x240
  vmovdqu [rsp + 32], ymm0
  vmovdqu [rsp + 64], ymm1
  vmovdqu [rsp + 96], ymm2
  vmovdqu [rsp + 128], ymm3
  vmovdqu [rsp + 160], ymm4
  vmovdqu [rsp + 192], ymm5
  vmovdqu [rsp + 224], ymm6
  vmovdqu [rsp + 256], ymm7
  vmovdqu [rsp + 288], ymm8
  vmovdqu [rsp + 320], ymm9
  vmovdqu [rsp + 352], ymm10
  vmovdqu [rsp + 384], ymm11
  vmovdqu [rsp + 416], ymm12
  vmovdqu [rsp + 448], ymm13
  vmovdqu [rsp + 480], ymm14
  vmovdqu [rsp + 512], ymm15
  lea rcx, [rbx - 0x28]
  mov r8d, 1
  vzeroupper
  call missileFlightLimit
  vmovss DWORD PTR [rsp + 64], xmm0
  vmovdqu ymm0, [rsp + 32]
  vmovdqu ymm1, [rsp + 64]
  vmovdqu ymm2, [rsp + 96]
  vmovdqu ymm3, [rsp + 128]
  vmovdqu ymm4, [rsp + 160]
  vmovdqu ymm5, [rsp + 192]
  vmovdqu ymm6, [rsp + 224]
  vmovdqu ymm7, [rsp + 256]
  vmovdqu ymm8, [rsp + 288]
  vmovdqu ymm9, [rsp + 320]
  vmovdqu ymm10, [rsp + 352]
  vmovdqu ymm11, [rsp + 384]
  vmovdqu ymm12, [rsp + 416]
  vmovdqu ymm13, [rsp + 448]
  vmovdqu ymm14, [rsp + 480]
  vmovdqu ymm15, [rsp + 512]
  add rsp, 0x240
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rbx
  pop rdx
  pop rcx
  pop rax
  popfq
  jmp QWORD PTR [rip + qaamFlightRangeReturn]
.globl lasmFlightRangeBridge
lasmFlightRangeBridge:
  vmulss xmm1, xmm7, DWORD PTR [rax + 0x14]
  pushfq
  push rax
  push rcx
  push rdx
  push rbx
  push rbp
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  sub rsp, 0x240
  vmovdqu [rsp + 32], ymm0
  vmovdqu [rsp + 64], ymm1
  vmovdqu [rsp + 96], ymm2
  vmovdqu [rsp + 128], ymm3
  vmovdqu [rsp + 160], ymm4
  vmovdqu [rsp + 192], ymm5
  vmovdqu [rsp + 224], ymm6
  vmovdqu [rsp + 256], ymm7
  vmovdqu [rsp + 288], ymm8
  vmovdqu [rsp + 320], ymm9
  vmovdqu [rsp + 352], ymm10
  vmovdqu [rsp + 384], ymm11
  vmovdqu [rsp + 416], ymm12
  vmovdqu [rsp + 448], ymm13
  vmovdqu [rsp + 480], ymm14
  vmovdqu [rsp + 512], ymm15
  lea rcx, [rbx - 0x28]
  mov r8d, 5
  vzeroupper
  call missileFlightLimit
  vmovss DWORD PTR [rsp + 64], xmm0
  vmovdqu ymm0, [rsp + 32]
  vmovdqu ymm1, [rsp + 64]
  vmovdqu ymm2, [rsp + 96]
  vmovdqu ymm3, [rsp + 128]
  vmovdqu ymm4, [rsp + 160]
  vmovdqu ymm5, [rsp + 192]
  vmovdqu ymm6, [rsp + 224]
  vmovdqu ymm7, [rsp + 256]
  vmovdqu ymm8, [rsp + 288]
  vmovdqu ymm9, [rsp + 320]
  vmovdqu ymm10, [rsp + 352]
  vmovdqu ymm11, [rsp + 384]
  vmovdqu ymm12, [rsp + 416]
  vmovdqu ymm13, [rsp + 448]
  vmovdqu ymm14, [rsp + 480]
  vmovdqu ymm15, [rsp + 512]
  add rsp, 0x240
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rbx
  pop rdx
  pop rcx
  pop rax
  popfq
  jmp QWORD PTR [rip + lasmFlightRangeReturn]
.globl saamFlightRangeBridge
saamFlightRangeBridge:
  vmulss xmm1, xmm8, DWORD PTR [rax + 0x14]
  pushfq
  push rax
  push rcx
  push rdx
  push rbx
  push rbp
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  sub rsp, 0x240
  vmovdqu [rsp + 32], ymm0
  vmovdqu [rsp + 64], ymm1
  vmovdqu [rsp + 96], ymm2
  vmovdqu [rsp + 128], ymm3
  vmovdqu [rsp + 160], ymm4
  vmovdqu [rsp + 192], ymm5
  vmovdqu [rsp + 224], ymm6
  vmovdqu [rsp + 256], ymm7
  vmovdqu [rsp + 288], ymm8
  vmovdqu [rsp + 320], ymm9
  vmovdqu [rsp + 352], ymm10
  vmovdqu [rsp + 384], ymm11
  vmovdqu [rsp + 416], ymm12
  vmovdqu [rsp + 448], ymm13
  vmovdqu [rsp + 480], ymm14
  vmovdqu [rsp + 512], ymm15
  lea rcx, [rbx - 0x28]
  mov r8d, 4
  vzeroupper
  call missileFlightLimit
  vmovss DWORD PTR [rsp + 64], xmm0
  vmovdqu ymm0, [rsp + 32]
  vmovdqu ymm1, [rsp + 64]
  vmovdqu ymm2, [rsp + 96]
  vmovdqu ymm3, [rsp + 128]
  vmovdqu ymm4, [rsp + 160]
  vmovdqu ymm5, [rsp + 192]
  vmovdqu ymm6, [rsp + 224]
  vmovdqu ymm7, [rsp + 256]
  vmovdqu ymm8, [rsp + 288]
  vmovdqu ymm9, [rsp + 320]
  vmovdqu ymm10, [rsp + 352]
  vmovdqu ymm11, [rsp + 384]
  vmovdqu ymm12, [rsp + 416]
  vmovdqu ymm13, [rsp + 448]
  vmovdqu ymm14, [rsp + 480]
  vmovdqu ymm15, [rsp + 512]
  add rsp, 0x240
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rbx
  pop rdx
  pop rcx
  pop rax
  popfq
  jmp QWORD PTR [rip + saamFlightRangeReturn]
.globl lagmFlightRangeBridge
lagmFlightRangeBridge:
  vmulss xmm1, xmm0, DWORD PTR [rip + lagmFlightNativeScale]
  pushfq
  push rax
  push rcx
  push rdx
  push rbx
  push rbp
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  sub rsp, 0x240
  vmovdqu [rsp + 32], ymm0
  vmovdqu [rsp + 64], ymm1
  vmovdqu [rsp + 96], ymm2
  vmovdqu [rsp + 128], ymm3
  vmovdqu [rsp + 160], ymm4
  vmovdqu [rsp + 192], ymm5
  vmovdqu [rsp + 224], ymm6
  vmovdqu [rsp + 256], ymm7
  vmovdqu [rsp + 288], ymm8
  vmovdqu [rsp + 320], ymm9
  vmovdqu [rsp + 352], ymm10
  vmovdqu [rsp + 384], ymm11
  vmovdqu [rsp + 416], ymm12
  vmovdqu [rsp + 448], ymm13
  vmovdqu [rsp + 480], ymm14
  vmovdqu [rsp + 512], ymm15
  lea rcx, [rdi - 0x28]
  mov r8d, 6
  vzeroupper
  call missileFlightLimit
  vmovss DWORD PTR [rsp + 64], xmm0
  vmovdqu ymm0, [rsp + 32]
  vmovdqu ymm1, [rsp + 64]
  vmovdqu ymm2, [rsp + 96]
  vmovdqu ymm3, [rsp + 128]
  vmovdqu ymm4, [rsp + 160]
  vmovdqu ymm5, [rsp + 192]
  vmovdqu ymm6, [rsp + 224]
  vmovdqu ymm7, [rsp + 256]
  vmovdqu ymm8, [rsp + 288]
  vmovdqu ymm9, [rsp + 320]
  vmovdqu ymm10, [rsp + 352]
  vmovdqu ymm11, [rsp + 384]
  vmovdqu ymm12, [rsp + 416]
  vmovdqu ymm13, [rsp + 448]
  vmovdqu ymm14, [rsp + 480]
  vmovdqu ymm15, [rsp + 512]
  add rsp, 0x240
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rbx
  pop rdx
  pop rcx
  pop rax
  popfq
  jmp QWORD PTR [rip + lagmFlightRangeReturn]
.globl sodFlightRangeBridge
sodFlightRangeBridge:
  vmulss xmm1, xmm0, DWORD PTR [rip + sodFlightNativeScale]
  pushfq
  push rax
  push rcx
  push rdx
  push rbx
  push rbp
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  sub rsp, 0x240
  vmovdqu [rsp + 32], ymm0
  vmovdqu [rsp + 64], ymm1
  vmovdqu [rsp + 96], ymm2
  vmovdqu [rsp + 128], ymm3
  vmovdqu [rsp + 160], ymm4
  vmovdqu [rsp + 192], ymm5
  vmovdqu [rsp + 224], ymm6
  vmovdqu [rsp + 256], ymm7
  vmovdqu [rsp + 288], ymm8
  vmovdqu [rsp + 320], ymm9
  vmovdqu [rsp + 352], ymm10
  vmovdqu [rsp + 384], ymm11
  vmovdqu [rsp + 416], ymm12
  vmovdqu [rsp + 448], ymm13
  vmovdqu [rsp + 480], ymm14
  vmovdqu [rsp + 512], ymm15
  mov rcx, rdi
  mov r8d, 8
  vzeroupper
  call missileFlightLimit
  vmovss DWORD PTR [rsp + 64], xmm0
  vmovdqu ymm0, [rsp + 32]
  vmovdqu ymm1, [rsp + 64]
  vmovdqu ymm2, [rsp + 96]
  vmovdqu ymm3, [rsp + 128]
  vmovdqu ymm4, [rsp + 160]
  vmovdqu ymm5, [rsp + 192]
  vmovdqu ymm6, [rsp + 224]
  vmovdqu ymm7, [rsp + 256]
  vmovdqu ymm8, [rsp + 288]
  vmovdqu ymm9, [rsp + 320]
  vmovdqu ymm10, [rsp + 352]
  vmovdqu ymm11, [rsp + 384]
  vmovdqu ymm12, [rsp + 416]
  vmovdqu ymm13, [rsp + 448]
  vmovdqu ymm14, [rsp + 480]
  vmovdqu ymm15, [rsp + 512]
  add rsp, 0x240
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rbx
  pop rdx
  pop rcx
  pop rax
  popfq
  jmp QWORD PTR [rip + sodFlightRangeReturn]
.att_syntax prefix

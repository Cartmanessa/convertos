org 0x7C00
use16

start:
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, start

    call select_color
    push ax
    xor ax, ax
    mov es, ax
    pop ax
    mov [es:0x1000], al

    mov bx, 0x1100
    mov es, bx
    xor bx, bx
    mov ah, 0x02
    mov al, 0x0F
    mov cx, 0x02
    mov dx, 0x01
    int 0x13

    mov bx, 0x1300
    mov es, bx
    xor bx, bx
    mov ah, 0x02
    mov al, 0x07
    mov cx, 0x011
    mov dx, 0x01
    int 0x13

    cli
    lgdt [gdt_info]

    in al, 0x92
    or al, 2
    out 0x92, al

    mov eax, cr0
    or al, 1
    mov cr0, eax

    jmp 0x08:protected_mode

select_color:
    xor bx, bx
.next:
    call clear_screen
    mov si, msg_select
    call print_string

    push bx
    mov si, bx
    shl si, 1
    add si, colors_list
    mov si, [si]
    call print_string
    pop bx

    mov ah, 0
    int 0x16

    cmp al, 0x0D
    je .done

    cmp al, 0
    jne .next

    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    jmp .next

.up:
    inc bx
    cmp bx, 6
    jl .next
    xor bx, bx
    jmp .next
.down:
    dec bx
    cmp bx, -1
    jge .next
    mov bx, 5
    jmp .next

.done:
    mov al, [color_attrs + bx]
    ret

print_string:
    mov ah, 0x0E
    xor bh, bh
.loop:
    lodsb
    test al, al
    jz .done1
    int 0x10
    jmp .loop
.done1:
    ret

clear_screen:
    mov ax, 0x0600
    xor cx, cx
    mov dx, 0x184F
    mov bh, 0x07
    int 0x10
    mov ah, 0x02
    xor bh, bh
    xor dx, dx
    int 0x10
    ret

msg_select  db 'Select color (arrows up/down, Enter to confirm): ', 0
colors_list:
    dw gray_str, white_str, yellow_str, blue_str, red_str, green_str
gray_str    db 'gray', 0
white_str   db 'white', 0
yellow_str  db 'yellow', 0
blue_str    db 'blue', 0
red_str     db 'red', 0
green_str   db 'green', 0

color_attrs:
    db 0x07
    db 0x0F
    db 0x0E
    db 0x09
    db 0x04
    db 0x02

gdt:
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0xff, 0xff, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00
    db 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00

gdt_info:
    dw $ - gdt
    dd gdt

use32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    call 0x11000
halt:
    hlt
    jmp halt

times 510 - ($-$$) db 0
dw 0xAA55

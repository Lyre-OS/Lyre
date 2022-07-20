#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <lib/panic.h>
#include <lib/print.h>
#include <sys/cpu.h>

noreturn void panic(struct cpu_ctx *ctx, const char *fmt, ...) {
    // TODO replace with some abort IPI and whatnot
    asm volatile ("cli");

    print("\n\n*** LYRE PANIC ***\n\n");

    print("The Lyre kernel panicked with the following message:\n  ");

    va_list args;

    va_start(args, fmt);
    vprint(fmt, args);
    va_end(args);

    print("\n\n");

    if (ctx == NULL) {
        goto halt;
    }

    print("CPU context at panic:\n");
    print("  RAX=%016lx  RBX=%016lx\n"
          "  RCX=%016lx  RDX=%016lx\n"
          "  RSI=%016lx  RDI=%016lx\n"
          "  RBP=%016lx  RSP=%016lx\n"
          "  R08=%016lx  R09=%016lx\n"
          "  R10=%016lx  R11=%016lx\n"
          "  R12=%016lx  R13=%016lx\n"
          "  R14=%016lx  R15=%016lx\n"
          "  RIP=%016lx  RFLAGS=%08lx\n"
          "  CS=%04lx DS=%04lx ES=%04lx SS=%04lx\n"
          "  ERR=%016lx",
          ctx->rax, ctx->rbx, ctx->rcx, ctx->rdx,
          ctx->rsi, ctx->rdi, ctx->rbp, ctx->rsp,
          ctx->r8, ctx->r9, ctx->r10, ctx->r11,
          ctx->r12, ctx->r13, ctx->r14, ctx->r15,
          ctx->rip, ctx->rflags,
          ctx->cs, ctx->ds, ctx->es, ctx->ss,
          ctx->err);

    print("\n\n");

halt:
    print("System halted.");

    for (;;) {
        asm volatile ("hlt");
    }
}

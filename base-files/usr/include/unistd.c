int panic(const char *fmt) // Kernel panic
{
  printf("Kernel panic: %s\n", fmt);
  asm volatile
  (
     "int $0x80"
     "hlt"
  );
  return (0);
}

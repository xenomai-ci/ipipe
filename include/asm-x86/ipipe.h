#if defined(CONFIG_IPIPE) && !defined(IPIPE_ARCH_STRING)
#define IPIPE_ARCH_STRING	"2.0-01"
#define IPIPE_MAJOR_NUMBER	2
#define IPIPE_MINOR_NUMBER	0
#define IPIPE_PATCH_NUMBER	1
#endif
#ifdef CONFIG_X86_32
# include "ipipe_32.h"
#else
# include "ipipe_64.h"
#endif

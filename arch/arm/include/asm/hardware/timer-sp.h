struct clk;

void __sp804_clocksource_and_sched_clock_init(void __iomem *,
<<<<<<< HEAD
					      unsigned long,
					      const char *, int);
=======
					      const char *, struct clk *, int);
void __sp804_clockevents_init(void __iomem *, unsigned int,
			      struct clk *, const char *);
>>>>>>> v3.10

static inline void sp804_clocksource_init(void __iomem *base, 
					  unsigned long phys, const char *name)
{
<<<<<<< HEAD
	__sp804_clocksource_and_sched_clock_init(base, phys, name, 0);
=======
	__sp804_clocksource_and_sched_clock_init(base, name, NULL, 0);
>>>>>>> v3.10
}

static inline void sp804_clocksource_and_sched_clock_init(void __iomem *base,
							  unsigned long phys,
							  const char *name)
{
<<<<<<< HEAD
	__sp804_clocksource_and_sched_clock_init(base, phys, name, 1);
=======
	__sp804_clocksource_and_sched_clock_init(base, name, NULL, 1);
>>>>>>> v3.10
}

static inline void sp804_clockevents_init(void __iomem *base, unsigned int irq, const char *name)
{
	__sp804_clockevents_init(base, irq, NULL, name);

}

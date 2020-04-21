/* Simulated clock seconds and nanoseconds*/
struct clock {
  unsigned int s;	 //seconds
	unsigned int ns; //nanoseconds
};

int  cmp_clocks(const struct clock *c, const struct clock *alarm);

// +, - and / for our clock type
void add_clocks(struct clock * c, const struct clock * c1);
void sub_clock(struct clock *c, const struct clock *c1, const struct clock *c2);
void div_clock(struct clock *c, const unsigned int c1);

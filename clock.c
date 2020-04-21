#include <stdlib.h>
#include "clock.h"

//nanoseconds in one second
#define MAX_NANOS 1000000000

// Like strcmp, but for sim_tm
int cmp_clocks(const struct clock *c, const struct clock *c1){
  if(	(c1->s  < c->s) ||
     ((c1->s == c->s) && (c1->ns <= c->ns)) ){
    return 1; //x < t
  }else{
    return 0; //x == t
  }
  return -1;  //x > t
}

//Add nano seconds to the clock
void add_clocks(struct clock * c, const struct clock * c1){
  c->s  = c->s + c1->s;
  c->ns = c->ns + c1->ns;
  if(c->ns > MAX_NANOS){
    c->s = c->s + 1;
    c->ns = c->ns % MAX_NANOS;
  }
}


// c = c1 - c2
void sub_clock(struct clock *c, const struct clock *c1, const struct clock *c2){

  c->s  = c1->s  - c2->s;
  if ((c1->ns - c2->ns) < 0) {
    c->s -= 1;
    c->ns = c2->ns - c1->ns;
  } else {
    c->ns = c1->ns - c2->ns;
  }
}

void div_clock(struct clock *c, const unsigned int x){
  if(x <= 0){
    return;
  }

  c->s     = c->s / x;
  c->ns = c->ns / x;
}

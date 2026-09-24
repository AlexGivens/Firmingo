#pragma once
#include <stdint.h>
typedef uint16_t u16_t;
typedef int err_t;
#define ERR_OK 0
typedef struct { uint8_t bytes[4]; } ip_addr_t;
#define IP_ADDR4(p,a,b,c,d) do { (p)->bytes[0]=(a); (p)->bytes[1]=(b); (p)->bytes[2]=(c); (p)->bytes[3]=(d); } while(0)
#define ip_2_ip4(p) ((p)->bytes)
#define ip_addr_copy(dst,src) ((dst)=(src))

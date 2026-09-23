/* X25519 scalar multiplication, extracted from TweetNaCl.
 *
 * Origin: https://tweetnacl.cr.yp.to/20140427/tweetnacl.c
 *   Version 20140427, by Daniel J. Bernstein, Bernard van Gastel, Wesley
 *   Janssen, Tanja Lange, Peter Schwabe and Sjaak Smetsers.
 *   sha256 of the downloaded tweetnacl.c:
 *   02e65bc3013ff2168983365e55906bc783c4c7e0a60d8100f17bb303a17175c4
 * License: public domain (https://tweetnacl.cr.yp.to/software.html).
 *
 * Everything below the marker line is copied byte for byte from that file:
 * lines 2-3, 5, 8-9, 273-318, 335-380 and 394-448, which are the helpers
 * crypto_scalarmult needs and nothing else. Two edits, both marked:
 *   - the constants _9 and _121665 are taken out of the multi-name
 *     declarations at lines 12-23, since the rest are unused and -Werror
 *     rejects unused statics;
 *   - the public names are mapped to mesh_ with #define, the same way
 *     tweetnacl.h maps them to crypto_scalarmult_curve25519_tweet, so the
 *     function bodies stay untouched.
 *
 * Not formatted by `ufbt format`: lint skips lib/, which is why this lives
 * here and not in src/proto. */
#include "mesh_x25519.h"

/* Edit: public names. */
#define crypto_scalarmult      mesh_x25519
#define crypto_scalarmult_base mesh_x25519_base

/* ---- TweetNaCl 20140427 below ---- */
#define FOR(i,n) for (i = 0;i < n;++i)
#define sv static void
typedef unsigned char u8;
typedef long long i64;
typedef i64 gf[16];

/* Edit: from the declarations at lines 12-23, only the two constants
   crypto_scalarmult uses, with their upstream values. */
static const u8 _9[32] = {9};
static const gf _121665 = {0xDB41,1};

sv car25519(gf o)
{
  int i;
  i64 c;
  FOR(i,16) {
    o[i]+=(1LL<<16);
    c=o[i]>>16;
    o[(i+1)*(i<15)]+=c-1+37*(c-1)*(i==15);
    o[i]-=c<<16;
  }
}

sv sel25519(gf p,gf q,int b)
{
  i64 t,i,c=~(b-1);
  FOR(i,16) {
    t= c&(p[i]^q[i]);
    p[i]^=t;
    q[i]^=t;
  }
}

sv pack25519(u8 *o,const gf n)
{
  int i,j,b;
  gf m,t;
  FOR(i,16) t[i]=n[i];
  car25519(t);
  car25519(t);
  car25519(t);
  FOR(j,2) {
    m[0]=t[0]-0xffed;
    for(i=1;i<15;i++) {
      m[i]=t[i]-0xffff-((m[i-1]>>16)&1);
      m[i-1]&=0xffff;
    }
    m[15]=t[15]-0x7fff-((m[14]>>16)&1);
    b=(m[15]>>16)&1;
    m[14]&=0xffff;
    sel25519(t,m,1-b);
  }
  FOR(i,16) {
    o[2*i]=t[i]&0xff;
    o[2*i+1]=t[i]>>8;
  }
}

sv unpack25519(gf o, const u8 *n)
{
  int i;
  FOR(i,16) o[i]=n[2*i]+((i64)n[2*i+1]<<8);
  o[15]&=0x7fff;
}

sv A(gf o,const gf a,const gf b)
{
  int i;
  FOR(i,16) o[i]=a[i]+b[i];
}

sv Z(gf o,const gf a,const gf b)
{
  int i;
  FOR(i,16) o[i]=a[i]-b[i];
}

sv M(gf o,const gf a,const gf b)
{
  i64 i,j,t[31];
  FOR(i,31) t[i]=0;
  FOR(i,16) FOR(j,16) t[i+j]+=a[i]*b[j];
  FOR(i,15) t[i]+=38*t[i+16];
  FOR(i,16) o[i]=t[i];
  car25519(o);
  car25519(o);
}

sv S(gf o,const gf a)
{
  M(o,a,a);
}

sv inv25519(gf o,const gf i)
{
  gf c;
  int a;
  FOR(a,16) c[a]=i[a];
  for(a=253;a>=0;a--) {
    S(c,c);
    if(a!=2&&a!=4) M(c,c,i);
  }
  FOR(a,16) o[a]=c[a];
}

int crypto_scalarmult(u8 *q,const u8 *n,const u8 *p)
{
  u8 z[32];
  i64 x[80],r,i;
  gf a,b,c,d,e,f;
  FOR(i,31) z[i]=n[i];
  z[31]=(n[31]&127)|64;
  z[0]&=248;
  unpack25519(x,p);
  FOR(i,16) {
    b[i]=x[i];
    d[i]=a[i]=c[i]=0;
  }
  a[0]=d[0]=1;
  for(i=254;i>=0;--i) {
    r=(z[i>>3]>>(i&7))&1;
    sel25519(a,b,r);
    sel25519(c,d,r);
    A(e,a,c);
    Z(a,a,c);
    A(c,b,d);
    Z(b,b,d);
    S(d,e);
    S(f,a);
    M(a,c,a);
    M(c,b,e);
    A(e,a,c);
    Z(a,a,c);
    S(b,a);
    Z(c,d,f);
    M(a,c,_121665);
    A(a,a,d);
    M(c,c,a);
    M(a,d,f);
    M(d,b,x);
    S(b,e);
    sel25519(a,b,r);
    sel25519(c,d,r);
  }
  FOR(i,16) {
    x[i+16]=a[i];
    x[i+32]=c[i];
    x[i+48]=b[i];
    x[i+64]=d[i];
  }
  inv25519(x+32,x+32);
  M(x+16,x+16,x+32);
  pack25519(q,x+16);
  return 0;
}

int crypto_scalarmult_base(u8 *q,const u8 *n)
{ 
  return crypto_scalarmult(q,n,_9);
}

/* tiny MPQS implementation, specially tuned for 128-bit input */

// #define TRACE -55867
// #define TRACE_P 937

#include "cado.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/resource.h>
#include "gmp.h"
#include "macros.h"
#include "mod_ul_default.h"

#define DIM 256

/* For i < 50, isprime_table[i] == 1 iff i is prime */
static unsigned char isprime_table[] = {
0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 
0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 
0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 
0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};

static const size_t isprime_table_size = 
    sizeof(isprime_table) / sizeof(isprime_table[0]);

long
cputime ()
{
  struct rusage rus;

  getrusage (RUSAGE_SELF, &rus);
  /* This overflows a 32 bit signed int after 2147483s = 24.85 days */
  return rus.ru_utime.tv_sec * 1000L + rus.ru_utime.tv_usec / 1000L;
}

int
jacobi (unsigned long a, unsigned long b)
{
  mpz_t aa, bb;
  int res;

  mpz_init_set_ui (aa, a);
  mpz_init_set_ui (bb, b);
  res = mpz_jacobi (aa, bb);
  mpz_clear (aa);
  mpz_clear (bb);
  return res;
}

/* return b^e mod n, assuming n has at most 32 bits */
static uint64_t
mod_pow_uint64 (uint64_t b, uint64_t e, uint64_t n)
{
  uint64_t r = 1, f = 1, y = b;

  while (f <= e)
    {
      if (e & f)
        r = (r * y) % n;
      f <<= 1;
      y = (y * y) % n;
    }
  
  return r;
}

/* Uses Tonelli-Shanks, more precisely Algorithm from Table 1 in [1].
   [1] Adleman-Manders-Miller Root Extraction Method Revisited, Zhengjun Cao,
   Qian Sha, Xiao Fan, 2011, http://arxiv.org/abs/1111.4877.
   Solve x^2 = rr (mod p).
*/
static uint64_t
tonelli_shanks (uint64_t rr, uint64_t p)
{
  uint64_t q, s, i, j, l;
  uint64_t aa, hh, delta, dd, bb, zz;

  if (p == 2)
    return rr;

  ASSERT_ALWAYS (p <= 4294967295UL);

  /* write p-1 = q*2^s with q odd */
  for (q = p-1, s = 0; (q&1) == 0; q/=2, s++);
  
  zz = 1;
  i = 1;
  do {
    /* zz is equal to i (mod p) */
    zz += 1;
    i++;
  } while (i < isprime_table_size && (!isprime_table[i] || jacobi (zz, p) != -1));
  ASSERT_ALWAYS (i < isprime_table_size);
  aa = mod_pow_uint64 (zz, q, p);
  bb = mod_pow_uint64 (rr, q, p);
  hh = 1;
  for (j = 1; j < s; j++)
    {
      dd = bb;
      for (l = 0; l < s - 1 - j; l++)
        dd = (dd * dd) % p;
      if (dd != 1)
        {
          hh = (hh * aa) % p;
          aa = (aa * aa) % p;
          bb = (bb * aa) % p;
        }
      else
        aa = (aa * aa) % p;
    }
  delta = mod_pow_uint64 (rr, (q + 1) >> 1, p);
  hh = (hh * delta) % p;

  return hh;
}

/* Given k1 such that k1^2 = N (mod p),
   return k1 and k2 which are the roots of (a*x+b)^2 = N (mod p).
   Assume a is odd.
*/
unsigned long
findroot (unsigned long *k2, unsigned long a, unsigned long bmodp,
          unsigned long p, unsigned long k1)
{
  /* special case for p=2: since a is odd, x = k1-b (mod 2) */
  if (p == 2)
    return (k1 - bmodp) & 1;

  /* the two roots are (k1-b)/a and (-k1-b)/a */
  modulus_t pp;
  residueul_t tt, uu, vv;
  modul_initmod_ul (pp, p);
  modul_init (tt, pp);
  modul_init (uu, pp);
  modul_init (vv, pp);
  modul_set_ul (tt, a, pp);
  modul_inv (tt, tt, pp); /* tt = 1/a mod p */
  modul_set_ul (vv, k1, pp);
  modul_neg (uu, vv, pp);
  modul_sub_ul (uu, uu, bmodp, pp);
  modul_mul (uu, uu, tt, pp);
  *k2 = mod_get_ul (uu, pp);
  modul_sub_ul (uu, vv, bmodp, pp);
  modul_mul (uu, uu, tt, pp);
  k1 = mod_get_ul (uu, pp);
  modul_clear (tt, pp);
  modul_clear (uu, pp);
  modul_clear (vv, pp);
  modul_clearmod (pp);
  if (*k2 < k1)
    {
      unsigned long tmp = *k2;
      *k2 = k1;
      return tmp;
    }
  return k1;
}

#define MAX_PRIMES 2048
unsigned int Primes[MAX_PRIMES] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599, 601, 607, 613, 617, 619, 631, 641, 643, 647, 653, 659, 661, 673, 677, 683, 691, 701, 709, 719, 727, 733, 739, 743, 751, 757, 761, 769, 773, 787, 797, 809, 811, 821, 823, 827, 829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919, 929, 937, 941, 947, 953, 967, 971, 977, 983, 991, 997, 1009, 1013, 1019, 1021, 1031, 1033, 1039, 1049, 1051, 1061, 1063, 1069, 1087, 1091, 1093, 1097, 1103, 1109, 1117, 1123, 1129, 1151, 1153, 1163, 1171, 1181, 1187, 1193, 1201, 1213, 1217, 1223, 1229, 1231, 1237, 1249, 1259, 1277, 1279, 1283, 1289, 1291, 1297, 1301, 1303, 1307, 1319, 1321, 1327, 1361, 1367, 1373, 1381, 1399, 1409, 1423, 1427, 1429, 1433, 1439, 1447, 1451, 1453, 1459, 1471, 1481, 1483, 1487, 1489, 1493, 1499, 1511, 1523, 1531, 1543, 1549, 1553, 1559, 1567, 1571, 1579, 1583, 1597, 1601, 1607, 1609, 1613, 1619, 1621, 1627, 1637, 1657, 1663, 1667, 1669, 1693, 1697, 1699, 1709, 1721, 1723, 1733, 1741, 1747, 1753, 1759, 1777, 1783, 1787, 1789, 1801, 1811, 1823, 1831, 1847, 1861, 1867, 1871, 1873, 1877, 1879, 1889, 1901, 1907, 1913, 1931, 1933, 1949, 1951, 1973, 1979, 1987, 1993, 1997, 1999, 2003, 2011, 2017, 2027, 2029, 2039, 2053, 2063, 2069, 2081, 2083, 2087, 2089, 2099, 2111, 2113, 2129, 2131, 2137, 2141, 2143, 2153, 2161, 2179, 2203, 2207, 2213, 2221, 2237, 2239, 2243, 2251, 2267, 2269, 2273, 2281, 2287, 2293, 2297, 2309, 2311, 2333, 2339, 2341, 2347, 2351, 2357, 2371, 2377, 2381, 2383, 2389, 2393, 2399, 2411, 2417, 2423, 2437, 2441, 2447, 2459, 2467, 2473, 2477, 2503, 2521, 2531, 2539, 2543, 2549, 2551, 2557, 2579, 2591, 2593, 2609, 2617, 2621, 2633, 2647, 2657, 2659, 2663, 2671, 2677, 2683, 2687, 2689, 2693, 2699, 2707, 2711, 2713, 2719, 2729, 2731, 2741, 2749, 2753, 2767, 2777, 2789, 2791, 2797, 2801, 2803, 2819, 2833, 2837, 2843, 2851, 2857, 2861, 2879, 2887, 2897, 2903, 2909, 2917, 2927, 2939, 2953, 2957, 2963, 2969, 2971, 2999, 3001, 3011, 3019, 3023, 3037, 3041, 3049, 3061, 3067, 3079, 3083, 3089, 3109, 3119, 3121, 3137, 3163, 3167, 3169, 3181, 3187, 3191, 3203, 3209, 3217, 3221, 3229, 3251, 3253, 3257, 3259, 3271, 3299, 3301, 3307, 3313, 3319, 3323, 3329, 3331, 3343, 3347, 3359, 3361, 3371, 3373, 3389, 3391, 3407, 3413, 3433, 3449, 3457, 3461, 3463, 3467, 3469, 3491, 3499, 3511, 3517, 3527, 3529, 3533, 3539, 3541, 3547, 3557, 3559, 3571, 3581, 3583, 3593, 3607, 3613, 3617, 3623, 3631, 3637, 3643, 3659, 3671, 3673, 3677, 3691, 3697, 3701, 3709, 3719, 3727, 3733, 3739, 3761, 3767, 3769, 3779, 3793, 3797, 3803, 3821, 3823, 3833, 3847, 3851, 3853, 3863, 3877, 3881, 3889, 3907, 3911, 3917, 3919, 3923, 3929, 3931, 3943, 3947, 3967, 3989, 4001, 4003, 4007, 4013, 4019, 4021, 4027, 4049, 4051, 4057, 4073, 4079, 4091, 4093, 4099, 4111, 4127, 4129, 4133, 4139, 4153, 4157, 4159, 4177, 4201, 4211, 4217, 4219, 4229, 4231, 4241, 4243, 4253, 4259, 4261, 4271, 4273, 4283, 4289, 4297, 4327, 4337, 4339, 4349, 4357, 4363, 4373, 4391, 4397, 4409, 4421, 4423, 4441, 4447, 4451, 4457, 4463, 4481, 4483, 4493, 4507, 4513, 4517, 4519, 4523, 4547, 4549, 4561, 4567, 4583, 4591, 4597, 4603, 4621, 4637, 4639, 4643, 4649, 4651, 4657, 4663, 4673, 4679, 4691, 4703, 4721, 4723, 4729, 4733, 4751, 4759, 4783, 4787, 4789, 4793, 4799, 4801, 4813, 4817, 4831, 4861, 4871, 4877, 4889, 4903, 4909, 4919, 4931, 4933, 4937, 4943, 4951, 4957, 4967, 4969, 4973, 4987, 4993, 4999, 5003, 5009, 5011, 5021, 5023, 5039, 5051, 5059, 5077, 5081, 5087, 5099, 5101, 5107, 5113, 5119, 5147, 5153, 5167, 5171, 5179, 5189, 5197, 5209, 5227, 5231, 5233, 5237, 5261, 5273, 5279, 5281, 5297, 5303, 5309, 5323, 5333, 5347, 5351, 5381, 5387, 5393, 5399, 5407, 5413, 5417, 5419, 5431, 5437, 5441, 5443, 5449, 5471, 5477, 5479, 5483, 5501, 5503, 5507, 5519, 5521, 5527, 5531, 5557, 5563, 5569, 5573, 5581, 5591, 5623, 5639, 5641, 5647, 5651, 5653, 5657, 5659, 5669, 5683, 5689, 5693, 5701, 5711, 5717, 5737, 5741, 5743, 5749, 5779, 5783, 5791, 5801, 5807, 5813, 5821, 5827, 5839, 5843, 5849, 5851, 5857, 5861, 5867, 5869, 5879, 5881, 5897, 5903, 5923, 5927, 5939, 5953, 5981, 5987, 6007, 6011, 6029, 6037, 6043, 6047, 6053, 6067, 6073, 6079, 6089, 6091, 6101, 6113, 6121, 6131, 6133, 6143, 6151, 6163, 6173, 6197, 6199, 6203, 6211, 6217, 6221, 6229, 6247, 6257, 6263, 6269, 6271, 6277, 6287, 6299, 6301, 6311, 6317, 6323, 6329, 6337, 6343, 6353, 6359, 6361, 6367, 6373, 6379, 6389, 6397, 6421, 6427, 6449, 6451, 6469, 6473, 6481, 6491, 6521, 6529, 6547, 6551, 6553, 6563, 6569, 6571, 6577, 6581, 6599, 6607, 6619, 6637, 6653, 6659, 6661, 6673, 6679, 6689, 6691, 6701, 6703, 6709, 6719, 6733, 6737, 6761, 6763, 6779, 6781, 6791, 6793, 6803, 6823, 6827, 6829, 6833, 6841, 6857, 6863, 6869, 6871, 6883, 6899, 6907, 6911, 6917, 6947, 6949, 6959, 6961, 6967, 6971, 6977, 6983, 6991, 6997, 7001, 7013, 7019, 7027, 7039, 7043, 7057, 7069, 7079, 7103, 7109, 7121, 7127, 7129, 7151, 7159, 7177, 7187, 7193, 7207, 7211, 7213, 7219, 7229, 7237, 7243, 7247, 7253, 7283, 7297, 7307, 7309, 7321, 7331, 7333, 7349, 7351, 7369, 7393, 7411, 7417, 7433, 7451, 7457, 7459, 7477, 7481, 7487, 7489, 7499, 7507, 7517, 7523, 7529, 7537, 7541, 7547, 7549, 7559, 7561, 7573, 7577, 7583, 7589, 7591, 7603, 7607, 7621, 7639, 7643, 7649, 7669, 7673, 7681, 7687, 7691, 7699, 7703, 7717, 7723, 7727, 7741, 7753, 7757, 7759, 7789, 7793, 7817, 7823, 7829, 7841, 7853, 7867, 7873, 7877, 7879, 7883, 7901, 7907, 7919, 7927, 7933, 7937, 7949, 7951, 7963, 7993, 8009, 8011, 8017, 8039, 8053, 8059, 8069, 8081, 8087, 8089, 8093, 8101, 8111, 8117, 8123, 8147, 8161, 8167, 8171, 8179, 8191, 8209, 8219, 8221, 8231, 8233, 8237, 8243, 8263, 8269, 8273, 8287, 8291, 8293, 8297, 8311, 8317, 8329, 8353, 8363, 8369, 8377, 8387, 8389, 8419, 8423, 8429, 8431, 8443, 8447, 8461, 8467, 8501, 8513, 8521, 8527, 8537, 8539, 8543, 8563, 8573, 8581, 8597, 8599, 8609, 8623, 8627, 8629, 8641, 8647, 8663, 8669, 8677, 8681, 8689, 8693, 8699, 8707, 8713, 8719, 8731, 8737, 8741, 8747, 8753, 8761, 8779, 8783, 8803, 8807, 8819, 8821, 8831, 8837, 8839, 8849, 8861, 8863, 8867, 8887, 8893, 8923, 8929, 8933, 8941, 8951, 8963, 8969, 8971, 8999, 9001, 9007, 9011, 9013, 9029, 9041, 9043, 9049, 9059, 9067, 9091, 9103, 9109, 9127, 9133, 9137, 9151, 9157, 9161, 9173, 9181, 9187, 9199, 9203, 9209, 9221, 9227, 9239, 9241, 9257, 9277, 9281, 9283, 9293, 9311, 9319, 9323, 9337, 9341, 9343, 9349, 9371, 9377, 9391, 9397, 9403, 9413, 9419, 9421, 9431, 9433, 9437, 9439, 9461, 9463, 9467, 9473, 9479, 9491, 9497, 9511, 9521, 9533, 9539, 9547, 9551, 9587, 9601, 9613, 9619, 9623, 9629, 9631, 9643, 9649, 9661, 9677, 9679, 9689, 9697, 9719, 9721, 9733, 9739, 9743, 9749, 9767, 9769, 9781, 9787, 9791, 9803, 9811, 9817, 9829, 9833, 9839, 9851, 9857, 9859, 9871, 9883, 9887, 9901, 9907, 9923, 9929, 9931, 9941, 9949, 9967, 9973, 10007, 10009, 10037, 10039, 10061, 10067, 10069, 10079, 10091, 10093, 10099, 10103, 10111, 10133, 10139, 10141, 10151, 10159, 10163, 10169, 10177, 10181, 10193, 10211, 10223, 10243, 10247, 10253, 10259, 10267, 10271, 10273, 10289, 10301, 10303, 10313, 10321, 10331, 10333, 10337, 10343, 10357, 10369, 10391, 10399, 10427, 10429, 10433, 10453, 10457, 10459, 10463, 10477, 10487, 10499, 10501, 10513, 10529, 10531, 10559, 10567, 10589, 10597, 10601, 10607, 10613, 10627, 10631, 10639, 10651, 10657, 10663, 10667, 10687, 10691, 10709, 10711, 10723, 10729, 10733, 10739, 10753, 10771, 10781, 10789, 10799, 10831, 10837, 10847, 10853, 10859, 10861, 10867, 10883, 10889, 10891, 10903, 10909, 10937, 10939, 10949, 10957, 10973, 10979, 10987, 10993, 11003, 11027, 11047, 11057, 11059, 11069, 11071, 11083, 11087, 11093, 11113, 11117, 11119, 11131, 11149, 11159, 11161, 11171, 11173, 11177, 11197, 11213, 11239, 11243, 11251, 11257, 11261, 11273, 11279, 11287, 11299, 11311, 11317, 11321, 11329, 11351, 11353, 11369, 11383, 11393, 11399, 11411, 11423, 11437, 11443, 11447, 11467, 11471, 11483, 11489, 11491, 11497, 11503, 11519, 11527, 11549, 11551, 11579, 11587, 11593, 11597, 11617, 11621, 11633, 11657, 11677, 11681, 11689, 11699, 11701, 11717, 11719, 11731, 11743, 11777, 11779, 11783, 11789, 11801, 11807, 11813, 11821, 11827, 11831, 11833, 11839, 11863, 11867, 11887, 11897, 11903, 11909, 11923, 11927, 11933, 11939, 11941, 11953, 11959, 11969, 11971, 11981, 11987, 12007, 12011, 12037, 12041, 12043, 12049, 12071, 12073, 12097, 12101, 12107, 12109, 12113, 12119, 12143, 12149, 12157, 12161, 12163, 12197, 12203, 12211, 12227, 12239, 12241, 12251, 12253, 12263, 12269, 12277, 12281, 12289, 12301, 12323, 12329, 12343, 12347, 12373, 12377, 12379, 12391, 12401, 12409, 12413, 12421, 12433, 12437, 12451, 12457, 12473, 12479, 12487, 12491, 12497, 12503, 12511, 12517, 12527, 12539, 12541, 12547, 12553, 12569, 12577, 12583, 12589, 12601, 12611, 12613, 12619, 12637, 12641, 12647, 12653, 12659, 12671, 12689, 12697, 12703, 12713, 12721, 12739, 12743, 12757, 12763, 12781, 12791, 12799, 12809, 12821, 12823, 12829, 12841, 12853, 12889, 12893, 12899, 12907, 12911, 12917, 12919, 12923, 12941, 12953, 12959, 12967, 12973, 12979, 12983, 13001, 13003, 13007, 13009, 13033, 13037, 13043, 13049, 13063, 13093, 13099, 13103, 13109, 13121, 13127, 13147, 13151, 13159, 13163, 13171, 13177, 13183, 13187, 13217, 13219, 13229, 13241, 13249, 13259, 13267, 13291, 13297, 13309, 13313, 13327, 13331, 13337, 13339, 13367, 13381, 13397, 13399, 13411, 13417, 13421, 13441, 13451, 13457, 13463, 13469, 13477, 13487, 13499, 13513, 13523, 13537, 13553, 13567, 13577, 13591, 13597, 13613, 13619, 13627, 13633, 13649, 13669, 13679, 13681, 13687, 13691, 13693, 13697, 13709, 13711, 13721, 13723, 13729, 13751, 13757, 13759, 13763, 13781, 13789, 13799, 13807, 13829, 13831, 13841, 13859, 13873, 13877, 13879, 13883, 13901, 13903, 13907, 13913, 13921, 13931, 13933, 13963, 13967, 13997, 13999, 14009, 14011, 14029, 14033, 14051, 14057, 14071, 14081, 14083, 14087, 14107, 14143, 14149, 14153, 14159, 14173, 14177, 14197, 14207, 14221, 14243, 14249, 14251, 14281, 14293, 14303, 14321, 14323, 14327, 14341, 14347, 14369, 14387, 14389, 14401, 14407, 14411, 14419, 14423, 14431, 14437, 14447, 14449, 14461, 14479, 14489, 14503, 14519, 14533, 14537, 14543, 14549, 14551, 14557, 14561, 14563, 14591, 14593, 14621, 14627, 14629, 14633, 14639, 14653, 14657, 14669, 14683, 14699, 14713, 14717, 14723, 14731, 14737, 14741, 14747, 14753, 14759, 14767, 14771, 14779, 14783, 14797, 14813, 14821, 14827, 14831, 14843, 14851, 14867, 14869, 14879, 14887, 14891, 14897, 14923, 14929, 14939, 14947, 14951, 14957, 14969, 14983, 15013, 15017, 15031, 15053, 15061, 15073, 15077, 15083, 15091, 15101, 15107, 15121, 15131, 15137, 15139, 15149, 15161, 15173, 15187, 15193, 15199, 15217, 15227, 15233, 15241, 15259, 15263, 15269, 15271, 15277, 15287, 15289, 15299, 15307, 15313, 15319, 15329, 15331, 15349, 15359, 15361, 15373, 15377, 15383, 15391, 15401, 15413, 15427, 15439, 15443, 15451, 15461, 15467, 15473, 15493, 15497, 15511, 15527, 15541, 15551, 15559, 15569, 15581, 15583, 15601, 15607, 15619, 15629, 15641, 15643, 15647, 15649, 15661, 15667, 15671, 15679, 15683, 15727, 15731, 15733, 15737, 15739, 15749, 15761, 15767, 15773, 15787, 15791, 15797, 15803, 15809, 15817, 15823, 15859, 15877, 15881, 15887, 15889, 15901, 15907, 15913, 15919, 15923, 15937, 15959, 15971, 15973, 15991, 16001, 16007, 16033, 16057, 16061, 16063, 16067, 16069, 16073, 16087, 16091, 16097, 16103, 16111, 16127, 16139, 16141, 16183, 16187, 16189, 16193, 16217, 16223, 16229, 16231, 16249, 16253, 16267, 16273, 16301, 16319, 16333, 16339, 16349, 16361, 16363, 16369, 16381, 16411, 16417, 16421, 16427, 16433, 16447, 16451, 16453, 16477, 16481, 16487, 16493, 16519, 16529, 16547, 16553, 16561, 16567, 16573, 16603, 16607, 16619, 16631, 16633, 16649, 16651, 16657, 16661, 16673, 16691, 16693, 16699, 16703, 16729, 16741, 16747, 16759, 16763, 16787, 16811, 16823, 16829, 16831, 16843, 16871, 16879, 16883, 16889, 16901, 16903, 16921, 16927, 16931, 16937, 16943, 16963, 16979, 16981, 16987, 16993, 17011, 17021, 17027, 17029, 17033, 17041, 17047, 17053, 17077, 17093, 17099, 17107, 17117, 17123, 17137, 17159, 17167, 17183, 17189, 17191, 17203, 17207, 17209, 17231, 17239, 17257, 17291, 17293, 17299, 17317, 17321, 17327, 17333, 17341, 17351, 17359, 17377, 17383, 17387, 17389, 17393, 17401, 17417, 17419, 17431, 17443, 17449, 17467, 17471, 17477, 17483, 17489, 17491, 17497, 17509, 17519, 17539, 17551, 17569, 17573, 17579, 17581, 17597, 17599, 17609, 17623, 17627, 17657, 17659, 17669, 17681, 17683, 17707, 17713, 17729, 17737, 17747, 17749, 17761, 17783, 17789, 17791, 17807, 17827, 17837, 17839, 17851, 17863};

#define INDEX 17864 /* should be larger than the last prime above */

int prime_index[INDEX];

uint64_t *invp;

/* Check that ((a*i+b)^2-N)/a is smooth. By construction, almost all inputs
   are smooth.
   We put relations in column 'shift' and above.
*/
int
is_smooth (mpz_t a, unsigned long i, mpz_t b, mpz_t N, unsigned long *P,
           unsigned long ncol, int shift, mpz_t row)
{
  mpz_t r;
  int res = 0;
  unsigned long B = P[ncol-1], R;
  unsigned long BB = B * B;

  mpz_set_ui (row, 0);
  mpz_init (r);
  mpz_mul_si (r, a, i);
  mpz_add (r, r, b);
  mpz_mul (r, r, r);
  mpz_sub (r, r, N);
  ASSERT (mpz_divisible_p (r, a));
  mpz_divexact (r, r, a);
  if (mpz_sgn (r) < 0)
    {
      mpz_setbit (row, shift + 0); /* sign is in column 0 */
      mpz_neg (r, r);
    }

  for (i = 0; i < ncol; i++)
    {
      unsigned long p = P[i];

      if (mpz_divisible_ui_p (r, p))
        {
          int e = 0;

          do {
            mpz_divexact_ui (r, r, p);
            e ++;
          } while (mpz_divisible_ui_p (r, p));

          if (e & 1)
            mpz_setbit (row, shift + i + 1);

          /* we don't check for cases where r = 1 or is a prime <= B here,
             since they will have very rarely */

          if (mpz_fits_ulong_p (r))
            {
              i = i + 1;
              break;
            }
        }
    }

  /* now r fits into an unsigned long */
  R = mpz_get_ui (r);
  for (; i < ncol; i++)
    {
      unsigned long p = P[i];
      unsigned long q = R * invp[i];
      unsigned long r0, r1;

      ularith_mul_ul_ul_2ul (&r0, &r1, q, p);
      if (r1 == 0)
        {
          int e = 0;
          do {
            R = R / p;
            e ++;
            q = R * invp[i];
            ularith_mul_ul_ul_2ul (&r0, &r1, q, p);
          } while (r1 == 0);
          if (e & 1)
            mpz_setbit (row, shift + i + 1);
          /* now since R has no factor <= p it is either 1 or a prime > p,
             or a product > p^2 of at least two primes  */
          if (R <= B)
            {
              /* R=1 might happen when the largest prime factor of R appears
                 with exponent 2 */
              if (R > 1)
                {
                  i = prime_index[R];
                  mpz_setbit (row, shift + i + 1);
                }
              res = 1;
              goto end;
            }
          else if (R <= BB)
            {
              /* if R = q0 * q1 with p < q0 < q1 < B, then q0 >= R/p */
              q = R / B;
              if (q > p)
                {
                  i = prime_index[q];
                  continue;
                }
            }
          /* we don't check for primality of R > B since it is rare */
        }
    }
 end:
  mpz_clear (r);
  return res;
}

static inline void
update (unsigned char *S, unsigned long i, unsigned long p MAYBE_UNUSED,
        unsigned char logp, unsigned int M MAYBE_UNUSED)
{
  S[i] += logp;
#ifdef TRACE
  if (i == M + TRACE)
    printf ("%d: %lu %u\n", TRACE, p, S[i]);
#endif
}

/* update 8 bytes at a time */
static inline void
update8 (unsigned char *S, unsigned long i, unsigned long p MAYBE_UNUSED,
        unsigned char logp, unsigned int M MAYBE_UNUSED)
{
  uint64_t l8, *S8 = (uint64_t*) S;

  l8 = logp;
  l8 = (i == 0) ? (l8 << 48) | (l8 << 32) | (l8 << 16) | l8
    : (l8 << 56) | (l8 << 40) | (l8 << 24) | (l8 << 8);
  for (i = 0; i < M; i+=8)
    S8[i>>3] += l8;
}

/* put factor in z */
void
gauss (mpz_t z, mpz_t *Mat, int nrel, int ncol, mpz_t *X, mpz_t N)
{
  int i, j, k, k2, j2;
  int shift = nrel; /* shift for the relations */
  mpz_t t;

  /* we put in the right part of the matrix (ncol columns) the matrix we
     want to put in row echelon form, and in the left part (nrel columns)
     the identity matrix: using this trick, the left part will reveal the
     relations at the end (and the right part will be the identity) */
  mpz_init (t);
  j = ncol - 1;
  i = 0;
  for (; j >= 1; )
    {
      /* try to zero all bits in column j, starting from row i */
      for (k = i; k < nrel; k++)
        if (mpz_tstbit (Mat[k], shift + j))
          break;
      if (k == nrel)
        {
          j -= 1;
          continue; /* go to next column */
        }
      if (i < k) /* swap rows i and k */
        mpz_swap (Mat[i], Mat[k]);
      /* we have found a pivot in (i,j) */
      j2 = j - 1;
    restart:
      if (j2 < 0)
        break; /* we still have to reduce column j */
      /* we must find a 2x2 matrix of rank 2, i.e., either [1,0;0,1] or
         [1,0;1,1] or [1,1;0,1] but not [1,1;1,1] */
      for (k2 = i + 1; k2 < nrel; k2++)
        if (mpz_tstbit (Mat[k2], shift + j2))
          {
            if (mpz_tstbit (Mat[k2], shift + j))
              mpz_xor (Mat[k2], Mat[k2], Mat[i]);
            /* now bit j is cleared */
            if (mpz_tstbit (Mat[k2], shift + j2))
              break;
          }
      if (k2 == nrel)
        {
          j2 --;
          goto restart;
        }
      if (i+1 < k2) /* swap rows i+1 and k2 */
        mpz_swap (Mat[i+1], Mat[k2]);

      /* clear bit j in Mat[i+1] */
      if (mpz_tstbit (Mat[i+1], shift + j))
        mpz_xor (Mat[i+1], Mat[i+1], Mat[i]);

      mpz_xor (t, Mat[i], Mat[i+1]);
      /* clear bit j2 in row i */
      if (mpz_tstbit (Mat[i], shift + j2))
        mpz_swap (t, Mat[i]);
      /* now we have a pivot in (i,j) and one in (i+1,j2) */
      for (k = i + 2; k < nrel; k++)
        {
          if (mpz_tstbit (Mat[k], shift + j))
            {
              if (mpz_tstbit (Mat[k], shift + j2))
                mpz_xor (Mat[k], Mat[k], t);
              else
                mpz_xor (Mat[k], Mat[k], Mat[i]);
            }
          else if (mpz_tstbit (Mat[k], shift + j2))
            mpz_xor (Mat[k], Mat[k], Mat[i+1]);
        }
      i += 2;
      j = j2 - 1;
    }
  /* finish by one row at a time */
  for (; j >= 0; j--)
    {
      for (k = i; k < nrel; k++)
        if (mpz_tstbit (Mat[k], shift + j))
          break;
      if (k == nrel)
        continue;
      if (i < k) /* swap rows i and k */
        mpz_swap (Mat[i], Mat[k]);
      for (k = i + 1; k < nrel; k++)
        {
          if (mpz_tstbit (Mat[k], shift + j))
            mpz_xor (Mat[k], Mat[k], Mat[i]);
        }
      i++;
    }
  mpz_clear (t);
  /* all rows from i to nrel-1 are dependencies */
  mpz_t x, y;
  mpz_init (x);
  mpz_init (y);
  printf ("Total %d dependencies\n", nrel - i);
  int i0 = i;
  while (i < nrel)
    {
      printf ("Trying dependency %d\n", i - i0);
      mpz_set_ui (x, 1);
      mpz_set_ui (y, 1);
      for (j = 0; j < nrel; j++)
        if (mpz_tstbit (Mat[i], j))
          {
            mpz_mul (x, x, X[j]);
            mpz_mul (z, X[j], X[j]);
            mpz_sub (z, z, N);
            mpz_mul (y, y, z);
          }
      ASSERT_ALWAYS (mpz_perfect_square_p (y));
      mpz_sqrt (y, y);
      mpz_sub (z, x, y);
      mpz_gcd (z, z, N);
      if (mpz_cmp_ui (z, 1) > 0 && mpz_cmp (z, N) < 0)
        {
          gmp_printf ("gcd=%Zd\n", z);
          break;
        }
      i++;
    }
  mpz_clear (x);
  mpz_clear (y);
}



/* Put in f a factor of N, using factor base of ncol primes.
   Assume N is odd. */
void
mpqs (mpz_t f, mpz_t N, long ncol)
{
  mpz_t *Mat, *X;
  unsigned char *S, *Logp, logp;
  unsigned long *P, p, q, k;
  long M, i, j, *K, wrel, nrel = 0;
  mpz_t a, b, c, sqrta;
  double maxnorm, radix, logradix;
  long st0 = cputime (), st, init_time, sieve_time = 0, check_time = 0;
  long gauss_time, total_time = 0;

  /* assume N is odd */
  ASSERT_ALWAYS (mpz_fdiv_ui (N, 2) == 1);

  mpz_init (a);
  mpz_init (sqrta);
  mpz_init (b);
  mpz_init (c);

  /* compute factor base */
  P = malloc (ncol * sizeof (unsigned long));
  K = malloc (ncol * sizeof (unsigned long));
  invp = malloc (ncol * sizeof (uint64_t));
  /* a prime p can appear in x^2 mod N only if p is a square modulo N */
  mpz_ui_pow_ui (b, 2, 64);
  memset (prime_index, 0, INDEX * sizeof (int));
  for (i = j = 0; j < ncol && i < MAX_PRIMES; i++)
    {
      unsigned long p = Primes[i];
      mpz_set_ui (a, p);
      /* a prime p can be in the factor base only if it is a square mod N */
      if (mpz_jacobi (a, N) == 1)
        {
          P[j] = p;
          prime_index[p] = j;
          for (int k = p - 1; k >= 0 && prime_index[k] == 0; k--)
            prime_index[k] = j;
          mpz_invert (c, a, b); /* c = 1/p mod 2^64 */
          invp[j] = mpz_get_ui (c);
          j++;
        }
    }
  ASSERT_ALWAYS(i < MAX_PRIMES);
  printf ("largest prime is %lu\n", P[ncol - 1]);
  wrel = ncol - 60; /* wanted number of relations */
  Mat = (mpz_t*) malloc (wrel * sizeof (mpz_t));
  for (i = 0; i < wrel; i++)
    mpz_init (Mat[i]);
  X = (mpz_t*) malloc (wrel * sizeof (mpz_t));
  for (i = 0; i < wrel; i++)
    mpz_init (X[i]);

  M = (1<<16); /* (half) size of sieve array */

#define GUARD 3
#define MAXS (255-GUARD)

  /* initialize sieve area [-M, M-1] */
  S = malloc (2 * M * sizeof (char));
  ASSERT_ALWAYS(((long) S & 7) == 0);
  Logp = malloc (ncol * sizeof (char));

  /* initialize square roots mod p */
  for (j = 0; j < ncol; j++)
    {
      unsigned long Np;
      p = P[j];
      Np = mpz_fdiv_ui (N, p);
      K[j] = tonelli_shanks (Np, p);
    }

  /* we want 'a' near sqrt(2*N)/M */
  mpz_mul_ui (a, N, 2);
  mpz_sqrt (a, a);
  mpz_tdiv_q_ui (a, a, M);

  /* we want 'a' to be a square */
  mpz_sqrt (sqrta, a);

  st = cputime ();
  printf ("init: %ldms\n", st);
  init_time = st - st0;

  int pols = 0;
  while (nrel < wrel) {
  sieve_time -= st;
  pols ++;
  do
    mpz_nextprime (sqrta, sqrta);
  while (mpz_jacobi (N, sqrta) != 1);
  /* we want N to be a square mod a: N = b^2 (mod a) */
  unsigned long aui = mpz_get_ui (sqrta);
  mpz_mul (a, sqrta, sqrta);
  /* we want b^2-N divisible by a */
  k = tonelli_shanks (mpz_fdiv_ui (N, aui), aui);
  /* we want b = k + a*t: b^2 = k^2 + 2*k*a*t (mod a^2), thus
     k^2 + 2*k*a*t = N (mod a^2): t = ((N-k^2)/a)/(2*k) mod a */
  mpz_ui_pow_ui (b, k, 2);
  mpz_sub (b, N, b);
  ASSERT_ALWAYS (mpz_divisible_p (b, sqrta));
  mpz_divexact (b, b, sqrta);
  mpz_set_ui (c, k);
  mpz_mul_ui (c, c, 2);
  mpz_invert (c, c, sqrta);
  mpz_mul (b, b, c);
  mpz_mod (b, b, sqrta);
  mpz_mul (b, b, sqrta);
  mpz_add_ui (b, b, k);
  // gmp_printf ("a=%Zd\nb=%Zd\n", a, b);
  unsigned long aa;
  ASSERT_ALWAYS(mpz_fits_ulong_p (a));
  aa = mpz_get_ui (a);

  /* we now have (a*x+b)^2 - N = a*Q(x) where Q(x) = a*x^2 + 2*b*x + c */

  /* initialize radix and Logp[]: we want radix^MAXS = maxnorm ~ N/a */
  maxnorm = mpz_get_d (N) / mpz_get_d (a);
  radix = pow (maxnorm, 1.0 / (double) MAXS);
  logradix = log (radix);
  for (j = 0; j < ncol; j++)
    Logp[j] = (char) (log ((double) P[j]) / logradix + 0.5);

  memset (S, 0, 2 * M * sizeof (char));

  /* sieve */
  mpz_submul_ui (b, a, M);
  for (j = 0; j < ncol; j++)
    {
      unsigned long Np, k2;
      long i2;
      p = P[j];
      k = K[j]; /* k^2 = N (mod p) */
#ifdef TRACE_P
      if (p == TRACE_P)
        printf ("p=%lu k=%lu\n", p, k);
#endif
      k = findroot (&k2, aa, mpz_fdiv_ui (b, p), p, k);
#ifdef TRACE_P
      if (p == TRACE_P)
        printf ("p=%lu k=%lu k2=%lu\n", p, k, k2);
#endif
      logp = Logp[j];
      if (p == 2)
        {
          /* Sieve 2, 4 and 8 simultaneously:
             if N = 1 (mod 8) then 8 divides x^2-N for x = 1, 3, 5, 7
             if N = 3 (mod 8) then 2 divides x^2-N for x = 1, 3, 5, 7
             if N = 5 (mod 8) then 4 divides x^2-N for x = 1, 3, 5, 7
             if N = 7 (mod 8) then 2 divides x^2-N for x = 1, 3, 5, 7.
          */
          Np = mpz_getlimbn (N, 0) & 7; /* N mod 8 */
          if (Np == 1) /* 2^3 always divides */
            logp = (char) (log ((double) 8) / logradix + 0.5);
          else if (Np == 5) /* 2^2 always divides */
            logp = (char) (log ((double) 4) / logradix + 0.5);
#if 0
          for (i = k; i < 2*M; i += 2)
            update (S, i, p, logp, M);
#else
          update8 (S, k, p, logp, M);
#endif
        }
      else
        {
          for (i = k, i2 = k2; i2 < 2*M; i += p, i2 += p)
            {
              update (S, i, p, logp, M);
              update (S, i2, p, logp, M);
            }
          if (i < 2*M)
            update (S, i, p, logp, M);

          /* sieve squares */
          q = p * p;
          if (q < P[ncol - 1])
            {
              unsigned long Ap, Bp;
              unsigned char logp2;
              logp2 = (char) (log ((double) q) / logradix + 0.5) - logp;
              Np = mpz_fdiv_ui (N, q);
              Ap = mpz_fdiv_ui (a, q);
              Bp = mpz_fdiv_ui (b, q);
              while (k < q)
                {
                  unsigned long y = (Ap * k + Bp) % q;
                  /* check if (a*k+b)^2 = N (mod q) */
                  if ((y * y) % q == Np)
                    for (i = k; i < 2*M; i += q)
                      update (S, i, p, logp2, M);
                  k += p;
                  if (k2 >= q)
                    break;
                  y = (Ap * k2 + Bp) % q;
                  if ((y * y) % q == Np)
                    for (i = k2; i < 2*M; i += q)
                      update (S, i, p, logp2, M);
                  k2 += p;
                }
            }
        }
    }

  st = cputime ();
  sieve_time += st;
  check_time -= st;

#ifdef TRACE
  printf ("%d: S=%d\n", TRACE, S[M + TRACE]);
#endif

  /* find smooth locations: the norm for location i is about 2*sqrt(N)*i */
  double Nd = mpz_get_d (N);
  /* we go from about sqrt(N) for i=0 to N/a for i=M. The magic constant 66
     here seems to be optimal for a 128-bit input. */
  unsigned char threshold = (unsigned char) (log (Nd) / logradix) + 66;
  for (i = -M; i < M && nrel < wrel; i++)
    {
      if (S[M + i] >= threshold)
        {
          if (is_smooth (a, M + i, b, N, P, ncol, wrel, Mat[nrel]))
            {
              mpz_setbit (Mat[nrel], nrel);
              mpz_set (X[nrel], b);
              mpz_addmul_ui (X[nrel], a, M + i);
              nrel ++;
              // printf ("%ld: i=%ld\n", nrel, i);
            }
        }
    }
  st = cputime ();
  check_time += st;
  gmp_printf ("sqrta=%Zd, total %ld rels in %ldms (%.2f r/s)\n",
              sqrta, nrel, st, (double) nrel / (double) st);
  }
  printf ("%ld rels with %d polynomials: %f per poly\n",
          nrel, pols, (double) nrel / (double) pols);

  gauss_time = st;
  gauss (f, Mat, nrel, ncol + 1, X, N);
  st = cputime ();
  gauss_time = st - gauss_time;
  total_time = st - st0;

  free (Logp);
  free (S);
  for (i = 0; i < wrel; i++)
    mpz_clear (Mat[i]);
  free (Mat);
  for (i = 0; i < wrel; i++)
    mpz_clear (X[i]);
  free (X);
  free (invp);
  free (K);
  free (P);
  mpz_clear (c);
  mpz_clear (b);
  mpz_clear (sqrta);
  mpz_clear (a);

  printf ("Total time: %ldms (init %ld, sieve %ld, check %ld, gauss %ld)\n",
          total_time, init_time, sieve_time, check_time, gauss_time);
}

/* On tarte.loria.fr:
$ ./mpqs 270788552349171139784543548689828248993 1000
gcd=15723507801130702049
Total time: 68ms (init 0, sieve 48, check 16, gauss 4)
*/
int
main (int argc, char *argv[])
{
  mpz_t N, f;
  unsigned long ncol;

  ASSERT_ALWAYS (argc > 2);
  ASSERT_ALWAYS (Primes[MAX_PRIMES - 1] < INDEX);
  mpz_init (N);
  mpz_init (f);
  mpz_set_str (N, argv[1], 10);
  ncol = atol (argv[2]);
  mpqs (f, N, ncol);
  mpz_clear (N);
  mpz_clear (f);
}

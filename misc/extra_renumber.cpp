#include "cado.h" // IWYU pragma: keep
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fcntl.h>   /* for _O_BINARY */
#include <fstream>
#include "cado_poly.h"  // cado_poly
#include "filter_io.h"  // earlyparsed_relation_ptr
#include "gzip.h"       // set_antebuffer_path
#include "misc.h"       // filelist
#include "params.h"     // param_list
#include "renumber.hpp" // renumber_t
#include "rootfinder.h" // mpz_poly_roots_ulong
#include "typedefs.h"   // p_r_values_t
#include "macros.h"

/* This program creates an "appendix" to a renumber table. It's never
 * used in cado, it seems, so that the code is _most likely_ buggy.
 */

char *argv0; /* = argv[0] */

uint64_t *very_large_primes = NULL;
uint64_t nb_very_large_primes = 0;
uint64_t very_large_primes_alloc = 0;


void *
look_for_very_large_primes (void * context_data, earlyparsed_relation_ptr rel)
{

  uint64_t *lpb = (uint64_t *) context_data;
  for (weight_t i = 0; i < rel->nb; i++)
  {
    int side = (int) rel->primes[i].side;
    p_r_values_t p = rel->primes[i].p;
    if (p > lpb[side])
    {
      if (nb_very_large_primes == very_large_primes_alloc)
      {
        very_large_primes_alloc = very_large_primes_alloc << 1;
        very_large_primes = (uint64_t *) realloc
              (very_large_primes, very_large_primes_alloc * sizeof (uint64_t));
      }
      very_large_primes[nb_very_large_primes++] = p;
    }
  }

  return NULL;
}

static void declare_usage(param_list pl)
{
  param_list_decl_usage(pl, "out", "output file for extra renumbering table");
  param_list_decl_usage(pl, "poly", "input polynomial file");
  param_list_decl_usage(pl, "lpb0", "large prime bound on side 0");
  param_list_decl_usage(pl, "lpb1", "large prime bound on side 1");
  param_list_decl_usage(pl, "filelist", "file containing a list of input files");
  param_list_decl_usage(pl, "basepath", "path added to all file in filelist");
  param_list_decl_usage(pl, "force-posix-threads", "force the use of posix threads, do not rely on platform memory semantics");
  param_list_decl_usage(pl, "path_antebuffer", "path to antebuffer program");
}

static void
usage (param_list pl, char *argv0)
{
    param_list_print_usage(pl, argv0, stderr);
    exit(EXIT_FAILURE);
}

uint64_t
cmp64 (void * a, void *b)
{
  uint64_t u = *((uint64_t *) a);
  uint64_t v = *((uint64_t *) b);
  if (u < v) return -1;
  else if (u > v) return 1;
  else return 0;
}

int
main (int argc, char *argv[])
{
  argv0 = argv[0];
  std::vector<unsigned int> lpb_arg(2,0);
  uint64_t lpb[2] = {0, 0};
  cado_poly cpoly;

  param_list pl;
  param_list_init(pl);
  declare_usage(pl);
  argv++,argc--;

  param_list_configure_switch(pl, "force-posix-threads", &filter_rels_force_posix_threads);

#ifdef HAVE_MINGW
  _fmode = _O_BINARY;     /* Binary open for all files */
#endif

  if (argc == 0)
    usage (pl, argv0);

  for( ; argc ; ) {
      if (param_list_update_cmdline(pl, &argc, &argv)) { continue; }
      /* Since we accept file names freeform, we decide to never abort
       * on unrecognized options */
      break;
      // fprintf (stderr, "Unknown option: %s\n", argv[0]);
      // abort();
  }
  /* print command-line arguments */
  param_list_print_command_line (stdout, pl);
  fflush(stdout);

  param_list_parse_uint(pl, "lpb0", &lpb_arg[0]);
  param_list_parse_uint(pl, "lpb1", &lpb_arg[1]);
  
  const char * filelist = param_list_lookup_string(pl, "filelist");
  const char * basepath = param_list_lookup_string(pl, "basepath");
  const char * polyfilename = param_list_lookup_string(pl, "poly");
  const char * outname = param_list_lookup_string(pl, "out");
  const char * path_antebuffer = param_list_lookup_string(pl, "path_antebuffer");

  if (param_list_warn_unused(pl))
  {
    fprintf(stderr, "Error, unused parameters are given\n");
    usage(pl, argv0);
  }

  if (lpb_arg[0] == 0 || lpb_arg[1] == 0)
  {
    fprintf (stderr, "Error, missing -lpb0 or -lpb1 command line argument\n");
    usage (pl, argv0);
  }
  if (outname == NULL)
  {
    fprintf (stderr, "Error, missing -out command line argument\n");
    usage(pl, argv0);
  }
  if (polyfilename == NULL)
  {
    fprintf (stderr, "Error, missing -poly command line argument\n");
    usage(pl, argv0);
  }
  if (basepath && !filelist)
  {
    fprintf(stderr, "Error, -basepath only valid with -filelist\n");
    usage(pl, argv0);
  }
  if ((filelist != NULL) + (argc != 0) != 1) {
    fprintf(stderr, "Error, provide either -filelist or freeform file names\n");
    usage(pl, argv0);
  }

  set_antebuffer_path (argv0, path_antebuffer);

  cado_poly_init(poly);
  if (!cado_poly_read (poly, polyfilename))
  {
    fprintf (stderr, "Error reading polynomial file\n");
    exit (EXIT_FAILURE);
  }

  lpb[0] = 1 << lpb_arg[0];
  lpb[1] = 1 << lpb_arg[1];

  very_large_primes_alloc = 1024;
  very_large_primes = (uint64_t *)
                          malloc (very_large_primes_alloc * sizeof (uint64_t));
  ASSERT_ALWAYS (very_large_primes != NULL);

  renumber_t renumber_table(poly);
  renumber_table.set_lpb(lpb_arg);

  char ** files = filelist ? filelist_from_file (basepath, filelist, 0) : argv;

  /* compute the list of very large primes appearing in the relations. */
  filter_rels(files, (filter_rels_callback_t) &look_for_very_large_primes,
              (void *) lpb, EARLYPARSE_NEED_AB_DECIMAL | EARLYPARSE_NEED_PRIMES,
              NULL, NULL);

  qsort (very_large_primes, nb_very_large_primes, sizeof (uint64_t),
                                                        (__compar_fn_t) cmp64);

  std::ofstream r_out(outname);
  renumber_table.write_header(r_out);
  /* true, we didn't compute the bad ideals. But we need to stick in
   * there the proper format info saying "no bad ideals there"
   */
  renumber_table.write_bad_ideals(r_out);

  unsigned long prev = 0;
  std::vector<std::vector<unsigned long>> roots;

  for (uint64_t i = 0; i < nb_very_large_primes; i++)
  {
      unsigned long p = very_large_primes[i];
      if (p == prev) continue;
      printf ("%lu\n", p);
      for (int side = 0; side < 2; side++) {
          mpz_poly_srcptr f = poly->pols[side];
          roots.push_back(mpz_poly_roots(f, p));
          // Check for a projective root
          if (mpz_divisible_ui_p (f->coeff[f->deg], p))
              roots.back().push_back(p);
      }

      auto cook = renumber_table.cook(p, roots);
      renumber_table.use_cooked(cook);
      r_out << cook.text;
      prev = p;
  }


  if (filelist)
    filelist_clear(files);

  cado_poly_clear (poly);
  param_list_clear(pl);
  free (very_large_primes);
  return 0;
}

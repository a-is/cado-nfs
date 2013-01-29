#include "methods.h"

// each line is automatically generated by run_stat.sh
#define NB_METHOD 868
static const cofac_method_t method_list[NB_METHOD] = {
#include "list_methods.i"
#include "list_methods2.i"
};

static const float zero_sub[60] = {0,}; // fill in with zeros.


const float * max_prob(const float *p1, const float *p2) {
    float s1, s2;
    for(int i = 0; i < 60; ++i) {
        s1 += p1[i];
        s2 += p2[i];
    }
    return (s1 < s2) ? p2 : p1;
}


cofac_method_srcptr get_method_naive(int *cofac_range, prior_srcptr prior,
        float *acc_fail1, float *acc_fail5, float *acc_fail7, 
        float *acc_fail11, ppm1_history_ptr ppm1_history)
{
    int best_meth = -1;
    float best_res = 0;
    int nb_bits = (cofac_range[1]-cofac_range[0])/2;
    int nb_words = nb_bits / 64;

    float prob1[60], prob5[60], prob7[60], prob11[60];
    // Baysian thm
    for (int i = 0; i < 60; ++i) {
 prob1[i] = prior->prob1[i]*acc_fail1[i]/(1-prior->prob1[i]*(1-acc_fail1[i]));
 prob5[i] = prior->prob5[i]*acc_fail5[i]/(1-prior->prob5[i]*(1-acc_fail5[i]));
 prob7[i] = prior->prob7[i]*acc_fail7[i]/(1-prior->prob7[i]*(1-acc_fail7[i]));
 prob11[i] = prior->prob11[i]*acc_fail11[i]/(1-prior->prob11[i]*(1-acc_fail11[i]));
    }

    for (int i = 0; i < NB_METHOD; ++i) {
        const float *ppm1_sub1 = zero_sub;
        const float *ppm1_sub5 = zero_sub;
        const float *ppm1_sub7 = zero_sub;
        const float *ppm1_sub11= zero_sub;
        if (method_list[i]->type == PM1) {
            ppm1_sub1 = ppm1_history->pm1_success1;
            ppm1_sub5 = ppm1_history->pm1_success5;
            ppm1_sub7 = ppm1_history->pm1_success7;
            ppm1_sub11 = ppm1_history->pm1_success11;
        }
        if (method_list[i]->type == PP1_27) {
            ppm1_sub1 = ppm1_history->pm1_success1;
            ppm1_sub5 = ppm1_history->pp1_success5;
            ppm1_sub7 = ppm1_history->pm1_success7;
            ppm1_sub11 = ppm1_history->pp1_success11;
        }
        if (method_list[i]->type == PP1_65) {
            ppm1_sub1 = ppm1_history->pm1_success1;
            ppm1_sub5 = ppm1_history->pm1_success5;
            ppm1_sub7 = ppm1_history->pp1_success7;
            ppm1_sub11 = ppm1_history->pp1_success11;
        }

        float res = 0.0;
        for (int j = 0; j < 60; ++j) {
            float r;
            r  = prob1[j]  * (method_list[i]->success1[j] -ppm1_sub1[j]);
            r += prob5[j]  * (method_list[i]->success5[j] -ppm1_sub5[j]);
            r += prob7[j]  * (method_list[i]->success7[j] -ppm1_sub7[j]);
            r += prob11[j] * (method_list[i]->success11[j]-ppm1_sub11[j]);
            r /= 4;
            res += r;
        }
        res /= method_list[i]->ms[nb_words];
        if ((best_meth == -1) || (res > best_res)) {
            best_meth = i;
            best_res = res;
        }
    }
    if (method_list[best_meth]->type == PM1) {
        ppm1_history->pm1_success1 = method_list[best_meth]->success1;
        ppm1_history->pm1_success5 = method_list[best_meth]->success5;
        ppm1_history->pm1_success7 = method_list[best_meth]->success7;
        ppm1_history->pm1_success11= method_list[best_meth]->success11;
    }
#define MAX_EQUAL(p1, p2) do { p1 = max_prob((p1), (p2)); } while (0)
    if (method_list[best_meth]->type == PP1_27) {
       MAX_EQUAL(ppm1_history->pm1_success1 ,method_list[best_meth]->success1 );
       MAX_EQUAL(ppm1_history->pp1_success5 ,method_list[best_meth]->success5 );
       MAX_EQUAL(ppm1_history->pm1_success7 ,method_list[best_meth]->success7 );
       MAX_EQUAL(ppm1_history->pp1_success11,method_list[best_meth]->success11);
    }
    if (method_list[best_meth]->type == PP1_65) {
       MAX_EQUAL(ppm1_history->pm1_success1 ,method_list[best_meth]->success1 );
       MAX_EQUAL(ppm1_history->pm1_success5 ,method_list[best_meth]->success5 );
       MAX_EQUAL(ppm1_history->pp1_success7 ,method_list[best_meth]->success7 );
       MAX_EQUAL(ppm1_history->pp1_success11,method_list[best_meth]->success11);
    }
#undef MAX_EQUAL

    return &method_list[best_meth][0];
}


load alpha.sage

# implements the formula on pages 86-87 of Murphy's thesis
# s is the skewness
def MurphyE(f,g,s):
    K = 1000
    # Bf = 4*10^5; Bg = 10^5; area = 1e13
    Bf = 1e7; Bg = 5e6; area = 1e16 # values used by pol51opt.c
    df = f.degree()
    dg = g.degree()
    alpha_f = alpha(f,2000)
    alpha_g = alpha(g,2000) # pol51opt.c uses alpha=0 for the linear polynomial
    E = 0
    sx = sqrt(area*s)
    sy = sqrt(area/s)
    for i in range(K):
       theta_i = float(pi/K*(i+1/2))
       xi = cos(theta_i)*sx
       yi = sin(theta_i)*sy
       fi = f(x=xi/yi)*yi^df
       gi = g(x=xi/yi)*yi^dg
       ui = (log(abs(fi))+alpha_f)/log(Bf)
       vi = (log(abs(gi))+alpha_g)/log(Bg)
       v1 = dickman_rho(ui) * dickman_rho(vi)
       E += v1
    return E/K

# example: RSA-768 polynomials
#skewness 44204.72 norm 1.35e+28 alpha -7.30 Murphy_E 3.79e-09
R.<x> = PolynomialRing(ZZ)
f = 265482057982680*x^6+1276509360768321888*x^5-5006815697800138351796828*x^4-46477854471727854271772677450*x^3+6525437261935989397109667371894785*x^2-18185779352088594356726018862434803054*x-277565266791543881995216199713801103343120
g=34661003550492501851445829*x-1291187456580021223163547791574810881
s=44204.72

f1=314460*x^5-6781777312402*x^4-1897893500688827450*x^3+18803371566755928198084581*x^2+2993935114144585946202328612634*x-6740134391082766311453285355967829910
g1=80795283786995723*x-258705022998682066594976199123
s1=1779785.90
# gives MurphyE(f1,g1,s1) = 2.21357103514175e-12

f2=7317291119021626157431*x^4+112772956569732784974895541380*x^3-81294630185701852973301070347631534*x^2-100082786829098214520932412293102900*x-15739142409937639475538548077351461929625
g2=34661003550492501851445829*x-1291187456580021223163547791574810881
s2=44204.720
# MurphyE(f2,g2,s2) = 6.65285225657159e-15

def rho(x):
   if x < 0:
      raise ValueError, "x < 0"
   if x <= 1.0:
      return 1.0
   if x <= 2.0:
      x = 2 * x - 3.0
      return .59453489206592253066+(-.33333334045356381396+(.55555549124525324224e-1+(-.12345536626584334599e-1+(.30864445307049269724e-2+(-.82383544119364408582e-3+(.22867526556051420719e-3+(-.63554707267886054080e-4+(.18727717457043009514e-4+(-.73223168705152723341e-5+.21206262864513086463e-5*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 3.0:
      x = 2 * x - 5.0
      return .13031956190159661490+(-.11890697945147816983+(.45224027972316855319e-1+(-.97335515428611864000e-2+(.20773419306178919493e-2+(-.45596184871832967815e-3+(.10336375145598596368e-3+(-.23948090479838959902e-4+(.58842414590359757174e-5+(-.17744830412711835918e-5+.42395344408760226490e-6*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 4.0:
      x = 2 * x - 7.0
      return .16229593252987041396e-1+(-.18617080355889960242e-1+(.98231465619823138710e-2+(-.30890609038683816355e-2+(.67860233545741575724e-3+(-.13691972815251836202e-3+(.27142792736320271161e-4+(-.54020882619058856352e-5+(.11184852221470961276e-5+(-.26822513121459348050e-6+.53524419961799178404e-7*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 5.0:
      x = 2 * x - 9.0
      return .13701177421087314589e-2+(-.18032881447340571269e-2+(.11344648599460666353e-2+(-.44785452702335769551e-3+(.12312893763747451118e-3+(-.26025855424244979420e-4+(.49439561514862370128e-5+(-.89908455922138763242e-6+(.16408096470458054605e-6+(-.32859809253342863007e-7+.55954809519625267389e-8*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 6.0:
      x = 2 * x - 11.0
      return .86018611204769414547e-4+(-.12455615869107014099e-3+(.87629281627689306328e-4+(-.39688578340310188782e-4+(.12884593599298603240e-4+(-.31758435723373651519e-5+(.63480088103905159169e-6+(-.11347189099214240765e-6+(.19390866486609035589e-7+(-.34493644515794744033e-8+.52005397653635760845e-9*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 7.0:
      x = 2 * x - 13.0
      return .42503555236283394611e-5+(-.66168162622648216578e-5+(.50451140426428793943e-5+(-.25056278110193975867e-5+(.90780066639285274491e-6+(-.25409423435688445924e-6+(.56994106166489666850e-7+(-.10719440462879689904e-7+(.18244728209885020524e-8+(-.30691539036795813350e-9+.42848520313019466802e-10*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 8.0:
      x = 2 * x - 15.0
      return .17178674964103956208e-6+(-.28335703551339176870e-6+(.23000575057461623263e-6+(-.12233608702949464058e-6+(.47877500071532198696e-7+(-.14657787264925438894e-7+(.36368896326425931590e-8+(-.74970784803203153691e-9+(.13390834297490308260e-9+(-.22532330111335516039e-10+.30448478436493554124e-11*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 9.0:
      x = 2 * x - 17.0
      return .58405695885954012372e-8+(-.10105102932563601939e-7+(.86312378192120507636e-8+(-.48483948355429166070e-8+(.20129738917787451004e-8+(-.65800969013673567377e-9+(.17591641970556292848e-9+(-.39380340623747158286e-10+(.75914903051312238224e-11+(-.13345077002021028171e-11+.18138420114766605833e-12*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 10.0:
      x = 2 * x - 19.0
      return .17063527516986966493e-9+(-.30739839907061929947e-9+(.27401311519313991860e-9+(-.16103964872970941164e-9+(.70152211692303181302e-10+(-.24143747383189013980e-10+(.68287507297199575436e-11+(-.16282751120203734207e-11+(.33676191519721199042e-12+(-.63207992345353337190e-13+.88821884863349801119e-14*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   return 0.277017183772596 * x^(-x)

def check_rho(a,b,N):
   maxerr = -1
   for i in range(N):
      x = a + (b-a)*random()
      y = dickman_rho(x)
      z = rho(x)
      err = abs(z-y)
      if err>maxerr:
         maxerr=err
         print x, err

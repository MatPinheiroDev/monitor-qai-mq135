# Figura 4 do artigo: indice Rs/R0 nas salas I09 e K13, com a grade de aulas.
import os
import sys; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analise_degraus import *
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt, matplotlib.dates as mdates
from matplotlib.patches import Patch; from matplotlib.lines import Line2D
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':6.5,'axes.linewidth':0.6})
D=dt.datetime
RI,_,_=carregar('dados/planilha_completa_1710.csv','I09',{50,51},{51:D(2026,10,8,7,8,24)})
RK,_,_=carregar('dados/planilha_completa_1710.csv','K13',{59})
R0N={'I09':[(D(2026,10,7,6),1434),(D(2000,1,1),2145)],'K13':[(D(2026,10,16,6),2066),(D(2026,10,15,6),2091),(D(2000,1,1),2422)]}
def r0(sala,t):
    for t0,v in R0N[sala]:
        if t>=t0: return v
deg={'I09':[(D(2026,10,5,17,10),False),(D(2026,10,6,8,50),True),(D(2026,10,6,13,0),False),(D(2026,10,6,20,30),True),(D(2026,10,7,13,50),True),(D(2026,10,7,17,10),False)],
     'K13':[(D(2026,10,13,17,10),False),(D(2026,10,14,13,50),False),(D(2026,10,15,10,30),False),(D(2026,10,15,18,50),False)]}
fig,axs=plt.subplots(2,1,figsize=(8.4/2.54,9.0/2.54))
for ax,(sala,R,ini) in zip(axs,[('I09',RI,D(2026,10,5,14,26)),('K13',RK,D(2026,10,13,14,21))]):
    R=[x for x in R if x['t']>=ini]
    t=[x['t'] for x in R]; y=[x['rs']/r0(sala,x['t']) for x in R]
    ys=[st.median(y[max(0,i-5):i+5]) for i in range(len(y))]
    seg=[0]+[i+1 for i in range(len(t)-1) if (t[i+1]-t[i]).total_seconds()>3600]+[len(t)]
    for a,b in zip(seg[:-1],seg[1:]): ax.plot(t[a:b],ys[a:b],color='black',lw=0.6)
    tt=ini.replace(minute=0)
    while tt<t[-1]:
        if not livre(sala,tt): ax.axvspan(tt,tt+dt.timedelta(minutes=10),color='0.82',lw=0)
        tt+=dt.timedelta(minutes=10)
    ax.axhline(1,color='0.4',ls=':',lw=0.8)
    for d0,det in deg[sala]:
        ax.plot([d0],[1.62],marker='v',ms=4,color='black' if det else 'white',mec='black',mew=0.8)
    if sala=='I09':
        ax.axvspan(D(2026,10,7,22,33),D(2026,10,8,13,8),hatch='////',fill=False,color='0.55',lw=0)
        ax.text(D(2026,10,8,5,50),0.35,'sem energia\ne descarte',ha='center',fontsize=5.5,bbox=dict(fc='white',ec='none',pad=1))
    ax.set_ylim(0.2,1.75); ax.set_xlim(ini,t[-1])
    ax.set_ylabel('Índice R$_S$/R$_0$')
    ax.text(0.006,0.05,f'({"a" if sala=="I09" else "b"}) Sala {sala}',transform=ax.transAxes,fontsize=6.5,fontweight='bold',bbox=dict(fc='white',ec='none',pad=1))
    ax.xaxis.set_major_locator(mdates.HourLocator(byhour=[0])); ax.xaxis.set_minor_locator(mdates.HourLocator(byhour=[6,12,18]))
    ax.xaxis.set_major_formatter(mdates.DateFormatter('%d/%m')); ax.grid(alpha=.25,which='major')
axs[1].set_xlabel('Data (marcas menores a cada 6 h)')
fig.legend(handles=[Patch(color='0.82',label='aula prevista na grade'),Line2D([],[],marker='v',ls='',color='black',label='degrau detectado'),Line2D([],[],marker='v',ls='',mfc='white',mec='black',label='degrau não detectado')],loc='upper center',ncol=3,fontsize=5.6,frameon=False,handletextpad=0.3,columnspacing=0.8)
plt.tight_layout(rect=(0,0,1,0.95),h_pad=0.6); plt.savefig('figuras/fig4_indice.png',dpi=400)

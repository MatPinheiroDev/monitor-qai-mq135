# Figuras finais do artigo na largura de uma coluna REPA (8,4 cm) - 26/10
import numpy as np, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
from matplotlib.ticker import MultipleLocator
from PIL import Image, ImageOps, ImageDraw, ImageFont
CM=1/2.54; OUT='figuras/'
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':7,'axes.linewidth':0.7})
INK='#1f1f1f'; EDGE='#555555'; ACC='#1f4e79'

# ---------------- Figura 1a: diagrama de blocos
fig=plt.figure(figsize=(8.4*CM,7.8*CM)); ax=fig.add_axes([0,0,1,1]); ax.set_xlim(0,84); ax.set_ylim(0,78); ax.axis('off')
def box(x,y,w,h,txt,bold=None,fs=6.3,fc='white',ec=EDGE,lw=0.8):
    ax.add_patch(FancyBboxPatch((x,y),w,h,boxstyle='round,pad=0,rounding_size=1.2',fc=fc,ec=ec,lw=lw))
    ax.text(x+w/2,y+h/2,txt,ha='center',va='center',fontsize=fs,color=INK,linespacing=1.25,fontweight=bold or 'normal')
def arr(x1,y1,x2,y2,lbl=None,dx=1,ha='left'):
    ax.annotate('',xy=(x2,y2),xytext=(x1,y1),arrowprops=dict(arrowstyle='-|>',color=EDGE,lw=0.7,mutation_scale=6,shrinkA=0,shrinkB=0))
    if lbl: ax.text((x1+x2)/2+dx,(y1+y2)/2,lbl,fontsize=5.6,color='#555555',ha=ha,va='center')
# linha 1: fontes
box(2,62,24,10,'MQ-135\nresistência $R_S$',fs=6.5)
box(30,62,24,10,'Régua USB\n5 V',fs=6.5)
box(58,62,24,10,'DHT11\nT e UR',fs=6.5)
# linha 2: condicionamento
box(2,43,24,14,'$R_L$ = 135,4 kΩ\ndivisor 213/215 kΩ\nC = 4,7 nF',fs=6)
box(30,43,24,14,'divisor\n440/100 kΩ\n(mede $V_{CC}$)',fs=6)
arr(14,62,14,57); arr(42,62,42,57); arr(70,62,70,38.5,'GPIO4',dx=1)
# linha 3: ESP32
box(2,24,80,14.5,'',fc='#eaf0f6',ec=ACC,lw=1.1)
ax.text(42,34.6,'ESP32 DevKit V1',ha='center',va='center',fontsize=6.8,fontweight='bold',color=INK)
ax.text(42,28.4,'ADC1 (GPIO34 e GPIO35) · 1.500 amostras a cada 30 s\nlinha resumida gravada na flash (LittleFS)',ha='center',va='center',fontsize=5.9,color=INK,linespacing=1.3)
arr(14,43,14,38.5,'GPIO34',dx=1); arr(42,43,42,38.5,'GPIO35',dx=1)
# linha 4: nuvem
box(2,4,24,13,'Wi-Fi a cada ~6 h\nhora (NTP)\nenvio em lotes',fs=5.9)
box(30,4,24,13,'Planilha\nem nuvem\n(Apps Script)',fs=5.9)
box(58,4,24,13,'Página de\nsituação',fs=5.9)
arr(14,24,14,17); arr(26,10.5,30,10.5); arr(54,10.5,58,10.5)
ax.text(1,77.4,'(a)',fontsize=7,fontweight='bold',va='top')
fig.savefig(OUT+'fig1a_diagrama.png',dpi=400,facecolor='white'); plt.close(fig)

# ---------------- Figura 1b: foto do prototipo, girada e rotulada
im=ImageOps.exif_transpose(Image.open('fotos/prototipo_casa_1.jpg'))
s=im.size[0]/1506
im=im.crop([int(v*s) for v in (300,280,1270,1680)]).rotate(90,expand=True)   # paisagem: ESP32 a esquerda
W=1600; im=im.resize((W,int(im.size[1]*W/im.size[0])))
d=ImageDraw.Draw(im); f=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',46)
print('foto 1b',im.size)
im.save(OUT+'_fig1b_base.jpg',quality=90)

labs=[('ESP32',(170,700),(230,1000)),('Cabo USB',(140,360),(330,90)),('MQ-135',(880,980),(1150,1050)),('DHT11',(1380,750),(1470,610)),('Capacitor 4,7 nF',(930,540),(1020,90))]
for t,a,b in labs:
    d.line([a,b],fill=(255,200,0),width=6)
    d.ellipse([a[0]-12,a[1]-12,a[0]+12,a[1]+12],fill=(255,200,0),outline='black',width=3)
    tb=d.textbbox(b,t,font=f,anchor='mm')
    d.rectangle([tb[0]-14,tb[1]-10,tb[2]+14,tb[3]+10],fill='white',outline='black',width=3)
    d.text(b,t,font=f,fill='black',anchor='mm')
tb=d.textbbox((20,20),'(b)',font=f); d.rectangle([tb[0]-8,tb[1]-8,tb[2]+8,tb[3]+8],fill='white'); d.text((20,20),'(b)',font=f,fill='black')
im.save(OUT+'fig1b_prototipo.jpg',quality=88)

# ---------------- Figura 2: calibracao, largura de coluna
adc=np.array([333.0,646.4,861.2,1299.0,2199.4,3714.7]); v=np.array([0.1500,0.3000,0.4000,0.5900,0.9800,1.6225])
a,b=np.polyfit(adc,v,1); fit=a*adc+b; res=(fit-v)*1000; rms=float(np.sqrt((res**2).mean())); N=4095; fe=a*N+b
br=lambda fmt,*x:(fmt%x).replace('.',',')
fig,(ax,bx)=plt.subplots(2,1,figsize=(8.4*CM,7.0*CM),sharex=True,gridspec_kw={'height_ratios':[3,1],'hspace':0.12})
x=np.linspace(0,N,200)
ax.plot(x,2.2/N*x,ls=(0,(5,3)),lw=1.1,color='#8a8a8a',label='nominal (2,20 V)')
ax.plot(x,a*x+b,lw=1.2,color=ACC,label='ajuste afim')
ax.plot(adc,v,'o',ms=4,mfc='white',mec=ACC,mew=1.1,label='pontos medidos')
ax.annotate('',xy=(N,2.2),xytext=(N,fe),arrowprops=dict(arrowstyle='<->',color='#b03a2e',lw=0.9,shrinkA=0,shrinkB=0))
ax.text(N+90,(2.2+fe)/2,'+24 %',ha='left',va='center',fontsize=6.3,fontweight='bold',color='#b03a2e')
ax.text(N+90,2.2,'2,20 V',fontsize=6,color='#666666',va='center'); ax.text(N+90,fe,br('%.2f V',fe),fontsize=6,color=ACC,va='center',fontweight='bold')
ax.text(80,2.05,'V = 4,3332×10⁻⁴·N + 19,9 mV\nR² = 0,9997',fontsize=6,va='top',bbox=dict(boxstyle='round,pad=0.35',fc='#f7f7f5',ec='#d5d5d0',lw=0.6))
ax.set_ylabel('Tensão no pino (V)'); ax.set_ylim(0,2.35); ax.set_xlim(0,4700); ax.yaxis.set_major_locator(MultipleLocator(0.5))
ax.grid(axis='y',color='#e9e9e6',lw=0.6); ax.legend(loc='lower right',frameon=False,fontsize=6)
for sp in ('top','right'): ax.spines[sp].set_visible(False); bx.spines[sp].set_visible(False)
bx.axhspan(-rms,rms,color='#eef1f5'); bx.axhline(0,color='#666666',lw=0.7)
bx.vlines(adc,0,res,color=ACC,lw=0.9); bx.plot(adc,res,'o',ms=3.5,mfc='white',mec=ACC,mew=1)
bx.set_ylabel('Resíduo (mV)',fontsize=6); bx.set_xlabel('Contagem do conversor, N'); bx.set_ylim(-13,21); bx.yaxis.set_major_locator(MultipleLocator(10))
bx.xaxis.set_major_locator(MultipleLocator(1000))
fig.subplots_adjust(left=0.16,right=0.98,top=0.98,bottom=0.15)
fig.savefig(OUT+'fig2_calibracao.png',dpi=400,facecolor='white'); plt.close(fig)
print('fe',fe,'rms',rms)

# ---------------- Figura 3: instalacoes I09 e K13 lado a lado
A=ImageOps.exif_transpose(Image.open('fotos/I09_instalacao_0510.jpg')); B=ImageOps.exif_transpose(Image.open('fotos/K13_instalacao_1310.jpg'))
H=1300
def fit_h(im): return im.resize((int(im.size[0]*H/im.size[1]),H))
A=fit_h(A); B=fit_h(B); gap=24
C=Image.new('RGB',(A.size[0]+B.size[0]+gap,H),'white'); C.paste(A,(0,0)); C.paste(B,(A.size[0]+gap,0))
d=ImageDraw.Draw(C); f2=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',56)
for x0,t in ((14,'(a)'),(A.size[0]+gap+14,'(b)')):
    tb=d.textbbox((x0,14),t,font=f2); d.rectangle([tb[0]-8,tb[1]-8,tb[2]+8,tb[3]+8],fill='white'); d.text((x0,14),t,font=f2,fill='black')
C.save(OUT+'fig3_instalacoes.jpg',quality=85); print('fig3',C.size,A.size,B.size)

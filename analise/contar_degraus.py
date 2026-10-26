import datetime as dt
slots=['07:10','08:00','08:50','09:40','10:30','11:20','12:10','13:00','13:50','14:40','15:30','16:20','17:10','18:00','18:50','19:40','20:30','21:20']
G={'B02':{0:'AAAA..AAAAAAAAAAAA',1:'AAAAAA..AA..AAAAAA',2:'AAAAAAAAAAA.AAAAAA',3:'AAAAAA..AAAAAAAAAA',4:'AAAAAAAAAAAA..AAAA'},
   'I09':{0:'..AAAA..AA..AAAA..',1:'..AA...MM.......AA',2:'AAAAAA..AA..AAAAAA',3:'AAAA....AA..AAAA..',4:'............AA....'},
   'K13':{0:'AA..AA......AAAAAA',1:'..AAAA......AAAA..',2:'AAAAAA..AAAAAAAA..',3:'....AA........AAAA',4:'..............AA..'}}
def mins(s): h,m=map(int,s.split(':')); return h*60+m
def degraus(sala,ini,fim,lim=100,obs=30):
    val=ini+dt.timedelta(hours=6); out=[];fracos=[]
    d=ini.date()
    while d<=fim.date():
        wd=d.weekday()
        if wd<5:
            row=G[sala][wd]
            for k,c in enumerate(row):
                if c=='.' : continue
                if k>0 and row[k-1]!='.': continue
                start=dt.datetime.combine(d,dt.time(*divmod(mins(slots[k]),60)))
                j=k
                while j>0 and row[j-1]=='.': j-=1
                emp_start=dt.datetime.combine(d,dt.time(*divmod(mins(slots[j]),60)))
                emp_start=max(emp_start,val)
                vazio=(start-emp_start).total_seconds()/60
                if start+dt.timedelta(minutes=obs)>fim or start<val: continue
                if vazio>=lim: out.append((d.strftime('%a %d/%m'),slots[k],int(vazio)))
                elif vazio>=50: fracos.append((d.strftime('%a %d/%m'),slots[k],int(vazio)))
        d+=dt.timedelta(days=1)
    return out,fracos
D=dt.datetime
cen={'ATUAL':[('B02',D(2026,10,13,8,50),D(2026,10,16,15,0)),('K13',D(2026,10,19,8,50),D(2026,10,22,15,40))],
     'PROPOSTA':[('K13',D(2026,10,13,8,0),D(2026,10,16,15,0)),('B02',D(2026,10,19,10,0),D(2026,10,22,13,30))],
     'I09 real':[('I09',D(2026,10,5,8,26),D(2026,10,8,15,40))]}
for n,lst in cen.items():
    tot=0
    for s,a,b in lst:
        o,f=degraus(s,a,b); tot+=len(o)
        print(n,s,a.strftime('%d/%m %H:%M'),'->',b.strftime('%d/%m %H:%M'),'| degraus',len(o),o,'| fracos',f)
    print(n,'TOTAL',tot,'\n')

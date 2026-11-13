"""Prototype: remove a flattened, semi-transparent diagonal text watermark and restore line colours.
Model: blended = (1-a)*orig + (G-(1-a)), G = colour of the watermark over white (light grey glyph fill)."""
import pikepdf, math, collections, sys
def col(o): return tuple(round(float(x),2) for x in o if not isinstance(x,pikepdf.Name))
def mul(m,n):
    a,b,c,d,e,f=m; A,B,C,D,E,F=n
    return (a*A+b*C,a*B+b*D,c*A+d*C,c*B+d*D,e*A+f*C+E,e*B+f*D+F)
def scan(p):
    """returns text-block colours (with effective angle) and fill colour counts, over all pages"""
    tcol=collections.Counter(); fcol=collections.Counter()
    for pg in p.pages:
        ctm=(1,0,0,1,0,0); st=[]; fill=None; tm=(1,0,0,1,0,0); size=1
        for i in pikepdf.parse_content_stream(pg):
            op=str(i.operator); o=i.operands
            if op=='q': st.append((ctm,fill))
            elif op=='Q': ctm,fill=st.pop()
            elif op=='cm': ctm=mul(tuple(float(x) for x in o),ctm)
            elif op in('scn','sc','g','rg'): fill=col(o)
            elif op=='Tm': tm=tuple(float(x) for x in o)
            elif op=='Tf': size=float(o[1])
            elif op in('TJ','Tj'):
                m=mul(tm,ctm); ang=round(math.degrees(math.atan2(m[1],m[0])))%360
                tcol[(fill,ang,round(size*math.hypot(m[0],m[1]),1))]+=1
            elif op in('f','f*'): fcol[fill]+=1
    return tcol,fcol
def fit(p):
    tcol,fcol=scan(p)
    # watermark run = the most frequent (angle,size) among text; its lightest colour = G
    runs=collections.Counter()
    for (c,a,s),n in tcol.items(): runs[(a,s)]+=n
    (ang,size),_=runs.most_common(1)[0]
    cols={c:n for (c,a,s),n in tcol.items() if (a,s)==(ang,size)}
    G=max(cols,key=lambda c:min(c)) if cols else None   # lightest
    blended=[c for c in cols if c!=G]
    origs=[c for c in fcol if c not in cols and c!=G and fcol[c]>=3]
    best=None
    for k in range(5,60):
        al=k/100.0
        err=0
        for b in blended:
            err+=min(max(abs(b[j]-((1-al)*o[j]+G[j]-(1-al))) for j in range(min(len(b),3))) for o in origs if len(o)==len(b)) if origs else 9
        if best is None or err<best[0]: best=(err,al)
    return dict(angle=ang,size=size,G=G,blended=blended,alpha=best[1],err=best[0],origs=origs,count=fcol)
def restore_map(m):
    G,al=m['G'],m['alpha']; out={}
    for b in m['blended']:
        cand=[(max(abs(b[j]-((1-al)*o[j]+G[j]-(1-al))) for j in range(3)),o) for o in m['origs'] if len(o)==len(b)]
        ok=[(e,o) for e,o in cand if e<0.06]
        e,o=max(ok,key=lambda x:m['count'][x[1]]) if ok else min(cand)
        out[b]=(o if e<0.06 else tuple(round(min(1,max(0,(b[j]-(G[j]-(1-al)))/(1-al))),2) for j in range(3)),e)
    return out
def run(src,dst):
    p=pikepdf.open(src); m=fit(p); rm=restore_map(m)
    print("angle",m['angle'],"size",m['size'],"G",m['G'],"alpha",m['alpha'],"fit err",round(m['err'],3))
    for b,(o,e) in rm.items(): print("  blended",b,"->",o,"err",round(e,3))
    G=m['G']; st=collections.Counter()
    for pg in p.pages:
        new=[]; buf=[]; inbt=False; fill=None; c=None
        for i in pikepdf.parse_content_stream(pg):
            op=str(i.operator)
            if op=='BT': inbt=True; buf=[i]; c=None; continue
            if inbt:
                if op in('scn','sc','g','rg'):
                    v=col(i.operands); c=v
                    if v in rm: i=pikepdf.ContentStreamInstruction([float(x) for x in rm[v][0]],i.operator); st['recolour']+=1
                buf.append(i)
                if op=='ET':
                    inbt=False
                    if c==G: st['text removed']+=1
                    else: new+=buf
                continue
            if op in('scn','sc','g','rg'):
                v=col(i.operands); fill=v
                if v in rm: i=pikepdf.ContentStreamInstruction([float(x) for x in rm[v][0]],i.operator); fill=rm[v][0]; st['recolour']+=1
            if op in('f','f*') and fill==G:
                new.append(pikepdf.ContentStreamInstruction([],pikepdf.Operator('n'))); st['fill removed']+=1; continue
            new.append(i)
        pg.Contents=p.make_stream(pikepdf.unparse_content_stream(new))
    p.save(dst); print(dict(st))
if __name__=='__main__': run(sys.argv[1],sys.argv[2])

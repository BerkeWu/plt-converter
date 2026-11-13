import pikepdf, re, sys
src,dst,confirm_brand=sys.argv[1],sys.argv[2],sys.argv[3]=='1'
p=pikepdf.open(src); st={'img':0,'forms emptied':0,'strings':0,'icon blocks':0}
CONTACT=re.compile(rb'\d{8,}|[A-Za-z]{4,}[A-Za-z0-9_]*(?=$)')   # long digit run, or a handle-like word
BRAND={b'SNOW'}
done=set()
for pg in p.pages:
    xo=pg.Resources.get('/XObject',{})
    for k,v in list(xo.items()):
        if v.get('/Subtype')=='/Form' and v.objgen not in done:
            ops=[str(i.operator) for i in pikepdf.parse_content_stream(v)]
            if ops and set(ops)<= {'k','K','g','G','rg','RG','ri','gs','re','f','q','Q','cm'} and 'f' in ops and ops.count('re')<=2:
                v.write(b''); done.add(v.objgen); st['forms emptied']+=1
    ins=list(pikepdf.parse_content_stream(pg)); new=[]; k=0
    while k<len(ins):
        i=ins[k]; op=str(i.operator)
        if op=='Do' and str(xo.get(str(i.operands[0]),{}).get('/Subtype'))=='/Image': st['img']+=1; k+=1; continue
        if op=='BT':
            j=k
            while str(ins[j].operator)!='ET': j+=1
            blk=ins[k:j+1]; keep=[]; shown=False; icon=False
            for b in blk:
                bo=str(b.operator)
                if bo in('Tj','TJ'):
                    s=bytes(b.operands[0]) if bo=='Tj' else b''.join(bytes(x) for x in b.operands[0] if isinstance(x,pikepdf.String))
                    if CONTACT.search(s.strip()) and len(s.strip())>=7 or (confirm_brand and s.strip() in BRAND):
                        st['strings']+=1; continue
                    if confirm_brand and b'\xde' in s and len(s)<=5: st['icon blocks']+=1; icon=True; continue
                    shown=True
                keep.append(b)
            new+=[ins[k]]+keep+[ins[j]] if False else keep
            k=j+1; continue
        new.append(i); k+=1
    pg.Contents=p.make_stream(pikepdf.unparse_content_stream(new))
p.save(dst); print(st)

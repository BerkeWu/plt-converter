import pikepdf, sys
src,dst=sys.argv[1:3]; p=pikepdf.open(src); n=0
for pg in p.pages:
    ins=list(pikepdf.parse_content_stream(pg)); new=[]; k=0
    while k<len(ins):
        i=ins[k]
        if str(i.operator)=='BDC':
            j=k
            while j<len(ins) and str(ins[j].operator)!='EMC': j+=1
            blk=ins[k:j+1]
            if any(str(b.operator)=='Do' and str(b.operands[0]).startswith('/KSPX') for b in blk): n+=1; k=j+1; continue
        new.append(i); k+=1
    pg.Contents=p.make_stream(pikepdf.unparse_content_stream(new))
p.save(dst); print("watermark groups removed:",n)

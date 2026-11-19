import re
def function(s,name):
 m=re.search(r'static\s+(?:int|void|unsigned|pid_t|const char\s*\*|const struct nxl_look_info\s*\*)\s*(?:long\s+)?'+name+r'\([^;]*?\)\s*\{',s);assert m,name
 i=m.end();depth=1;state='code'
 while depth:
  c=s[i];q=s[i:i+2]
  if state=='code':
   if q=='/*':state='comment';i+=2;continue
   if q=='//':state='line';i+=2;continue
   if c=='"':state='string'
   elif c=="'":state='char'
   elif c=='{':depth+=1
   elif c=='}':depth-=1
  elif state=='comment':
   if q=='*/':state='code';i+=2;continue
  elif state=='line':
   if c=='\n':state='code'
  elif c=='\\':i+=2;continue
  elif(state=='string'and c=='"')or(state=='char'and c=="'"):state='code'
  i+=1
 return s[m.start():i]+'\n'

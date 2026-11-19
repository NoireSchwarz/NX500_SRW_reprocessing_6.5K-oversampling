from context import *
exec((A/'research_native.py').read_text('utf8').split('lines=[]')[0])
print('native GUI update key:',text(0x3c37ec))
for start,size in [(0x273a00,0x60),(0x286e78,0x160),(0x286d90,0x90)]:
 print('\nBLOCK',hex(start))
 for i in md.disasm(data(start,size),start):
  extra=''
  if i.mnemonic in ['b','bl']and i.op_str.startswith('#'):extra=' ; '+syms.get(int(i.op_str[1:],0),'')
  print(hex(i.address),i.mnemonic,i.op_str,extra)

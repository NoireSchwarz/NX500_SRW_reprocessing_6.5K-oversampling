from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc3-authoring/deploy_sd.py').read_text('utf8').replace('RC3','RC4').replace('rc3tmp-','rc4tmp-')
# The inherited deploy script substitutes RC1 globally. Add the third RC4 doc
# through its original replacement, preserving the closed SD target allowlist.
s=s.replace(".replace('rc1tmp-','rc4tmp-')", ".replace('rc1tmp-','rc4tmp-').replace('RC4_LUT_CATALOG_zh.txt','RC4_RELOAD_RESEARCH_zh.txt')")
exec(compile(s,str(here/'deploy_sd.py'),'exec'))

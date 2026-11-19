from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc2-authoring/publish_source.py').read_text('utf8').replace('RC2','RC3').replace('release_rc2','release_rc3')
s=s.replace("*(M/'bin').iterdir(),M/'README_zh.md'", "*(M/'bin').iterdir(),*(M/'looks').glob('*.bin'),M/'README_zh.md'")
s=s.replace('RC3_VIDEO_RELOAD_RESEARCH_zh.txt','RC3_LUT_CATALOG_zh.txt')
exec(compile(s,str(here/'publish_source.py'),'exec'))

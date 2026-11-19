from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc1-authoring/test_installer.py').read_text('utf8')
s=s.replace("[(W.parent/'r11p-clean-log-work/stage','R11p-original',R/'work/firmware/libmmfcore.so'),(B,'R11q-current-patched',M/'native/libmmfcore.r11f.so')]", "[(B,'RC2-current-patched',M/'native/libmmfcore.r11f.so')]")
s=s.replace('all_19_LUTs_installed','all_32_LUTs_installed')
exec(compile(s,str(here/'test_installer.py'),'exec'))

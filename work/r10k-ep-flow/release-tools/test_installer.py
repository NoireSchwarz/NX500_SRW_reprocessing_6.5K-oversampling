from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc3-authoring/test_installer.py').read_text('utf8').replace('RC2-current-patched','RC4-current-patched')
exec(compile(s,str(here/'test_installer.py'),'exec'))

from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc4-authoring/rebind.py').read_text('utf8').replace('RC4','RC5')
exec(compile(s,str(here/'rebind.py'),'exec'))

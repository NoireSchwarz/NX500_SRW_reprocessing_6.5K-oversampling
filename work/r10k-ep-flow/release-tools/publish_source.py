from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc4-authoring/publish_source.py').read_text('utf8').replace('RC4','RC5').replace('release_rc4','release_rc5')
exec(compile(s,str(here/'publish_source.py'),'exec'))

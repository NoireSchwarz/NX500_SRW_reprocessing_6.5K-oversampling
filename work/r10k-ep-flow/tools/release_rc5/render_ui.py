from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc4-authoring/render_ui.py').read_text('utf8')
exec(compile(s,str(here/'render_ui.py'),'exec'))

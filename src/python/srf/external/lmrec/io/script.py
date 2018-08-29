from jinja2 import Environment, PackageLoader
from jfs.api import Path

env = Environment(loader=PackageLoader('srf.external.lmrec', 'templates'))


def render(renderable):
    template = env.get_template(renderable.template)
    return renderable.render(template)

def save_script(path,data):
    path = Path(path)
    target = path.abs
    with open(target,'w') as fin:
        fin.write(data)
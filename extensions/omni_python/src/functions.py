import os
import ast
import decimal
import typing
import types
import sys
import hashlib
import importlib

# types.UnionType has only been available since Python 3.10
if sys.version_info >= (3, 10):
    from types import UnionType
else:
    from typing import Union as UnionType

site_packages = os.path.expanduser(
    plpy.execute(plpy.prepare("""
                     select coalesce(
                        (select value from omni_python.config where name = 'site_packages'),
                        (select (setting || '/.omni_python/default') as value from pg_settings where name = 'data_directory'))
                        as value
       """))[0]['value'])

if sys.path[0] != site_packages:
    sys.path.insert(0, site_packages)

importlib.invalidate_caches()

import omni_python

have_omni_vfs = False

imports = {}

# Consider omni_vfs as a source of custom imports
if fs is not None:
    have_omni_vfs = plpy.execute(
        plpy.prepare("select count(*) from pg_extension where extname = 'omni_vfs'")
    )[0]['count'] > 0
    if have_omni_vfs:
        import importlib.abc, importlib.machinery


        class CustomImporter(importlib.abc.MetaPathFinder, importlib.abc.Loader):
            def __init__(self, fs_type, fs):
                self.query = f"""
                             select $1 as filename from omni_vfs.file_info('{fs}'::{fs_type}, $1) where kind = 'file'
                             union
                             select $2 as filename from omni_vfs.file_info('{fs}'::{fs_type}, $2) where kind = 'file'
                             """

            def find_spec(self, fullname, path, target=None):
                results = plpy.execute(
                    plpy.prepare(self.query, ["text", "text"]),
                    [f"{os.path.join(*fullname.split('.'))}.py",
                     f"{os.path.join(*fullname.split('.'))}/__init__.py"]
                )
                if len(results) > 0:
                    return importlib.machinery.ModuleSpec(fullname, self, is_package=True)

            def create_module(self, spec):
                return types.ModuleType(spec.name)

            def exec_module(self, mod):
                result = plpy.execute(
                    plpy.prepare(
                        f"""with files as ({self.query})
                             select omni_vfs.read('{fs}'::{fs_type}, filename) as source, filename from files
                             """, ["text", "text"]),
                    [f"{os.path.join(*mod.__name__.split('.'))}.py",
                     f"{os.path.join(*mod.__name__.split('.'))}/__init__.py"])[0]
                module = compile(result["source"], result["filename"], "exec")
                imports[mod.__name__] = result["source"]
                exec(module, globals(), mod.__dict__)


        sys.meta_path.insert(0, CustomImporter(fs_type, fs))
#

code_locals = {}

hash = hashlib.sha256(code.encode()).hexdigest()
try:
    exec(compile(code, filename or 'unnamed.py', 'exec'), code_locals)
    if have_omni_vfs:
        sys.meta_path.pop(0)
    if '__omni_python_functions__' not in GD:
        GD['__omni_python__functions__'] = {}
    GD['__omni_python__functions__'][hash] = code_locals
except SyntaxError as e:
    # Is the syntax error here because this is a "legacy" Python function
    # (code you stick inside of `create function`'s body) as intended to be
    # used by bare plpython3u?
    try:
        import textwrap

        # Stick it into a function and see if it is valid now
        exec(compile(f"def __test_function_():\n{textwrap.indent(code, ' ')}",
                     filename or 'unnamed.py', 'exec'), {})
        # It is. Let the other handler do it as we won't extract anything
        return []
    except SyntaxError:
        # It's not. Re-raise the original error
        raise e

pg_functions = []
for name, value in code_locals.items():
    if not callable(value) or not dir(value):
        # This is a very implementation-specific fix for Flask's request
        # import that tries to self-initialize upon inspection. That said,
        # if `value`'s dir is empty then there's no `__pg_stored_procedure__`
        # and we can avoid calling `hasattr` that effectively attempts to initialize
        # the request and fails to do so, failing with a `RuntimeError`
        continue
    if hasattr(value, '__pg_stored_procedure__'):
        pg_functions.append((name, value, value.__pg_stored_procedure__))

__types__ = {str: 'text', bool: 'boolean', bytes: 'bytea', int: 'int',
             decimal.Decimal: 'numeric', float: 'double precision',
             omni_python.types.json: 'json'
             }


def resolve_type(function, arg):
    type = inspect.getfullargspec(function).annotations[arg]
    if hasattr(type, '__pg_type_hint__') and callable(type.__pg_type_hint__):
        type.__pg_type_hint__.__globals__.update(code_locals)
        type = type.__pg_type_hint__()
    if type is None:
        return 'unknown'
    else:
        # list
        if isinstance(type, types.GenericAlias) and type.__origin__ == list:
            if len(type.__args__) > 1:
                raise TypeError("lists can only be of one parameter")
            type = type.__args__[0]
            return '{type}[]'.format(type=__types__.get(type, 'unknown'))
        # Optional
        if type.__class__ in (typing.Optional[bool].__class__, UnionType) and len(
                type.__args__) == 2 and None.__class__ in type.__args__:
            type = [t for t in type.__args__ if t != None.__class__][0]
        # Custom type
        if type.__class__ == typing.Annotated[int, 0].__class__ and isinstance(type.__metadata__[0],
                                                                               omni_python.pgtype):
            return type.__metadata__[0].name
        try:
            return __types__.get(type, 'unknown')
        except TypeError:
            return 'unknown'


def process_argument(function, arg):
    type = inspect.getfullargspec(function).annotations[arg]
    if hasattr(type, '__pg_type_hint__') and callable(type.__pg_type_hint__):
        type.__pg_type_hint__.__globals__.update(code_locals)
        type = type.__pg_type_hint__()
    if (type.__class__ == typing.Annotated[int, 0].__class__ and isinstance(type.__metadata__[0],
                                                                            omni_python.pgtype) and
            type.__metadata__[1] == "composite"):
        klass = type.__args__[0]
        if klass.__module__ == '__main__':
            return f"{klass.__name__}(**{arg})"
        elif klass.__module__ == 'builtins':
            lookup = f"GD['__omni_python__functions__']['{hash}']"
            return f"{lookup}['{klass.__name__}'](**{arg})"
        else:
            lookup = ast.unparse(
                ast.Subscript(value=ast.Name(id='sys.modules', ctx=ast.Load()),
                              slice=ast.Constant(value=klass.__module__),
                              ctx=ast.Load()))
            return f"{lookup}.{klass.__name__}(**{arg})"
    else:
        return arg


site_packages = plpy.quote_literal(site_packages)

import inspect

from textwrap import dedent

imports_assignment = ast.Dict([ast.Constant(module) for module, source in imports.items()],
                              [ast.Constant(source) for module, source in imports.items()])
loader = dedent(f"""
    class CustomImporter(__import__('importlib.abc').abc.MetaPathFinder, __import__('importlib.abc').abc.Loader):
        __imports = {ast.unparse(imports_assignment)}
        def find_spec(self, fullname, path, target=None):
            mod = self.__imports.get(fullname)
            if mod:
               return __import__('importlib.machinery').machinery.ModuleSpec(fullname, self, is_package=True)

        def create_module(self, spec):
            import types
            return types.ModuleType(spec.name)

        def exec_module(self, mod):
            source = self.__imports[mod.__name__]
            exec(source, globals(), mod.__dict__)
    sys.meta_path.insert(0, CustomImporter())
    del CustomImporter
    """)

return [(pgargs.get('name', name),
         [a for a in inspect.getfullargspec(f).args if a != 'self'],
         [resolve_type(f, a) for a in inspect.getfullargspec(f).args if a != 'self'], resolve_type(f, 'return'),
         dedent("""
             import sys
             if '__omni_python__functions__' in GD:
                 if '{hash}' in GD['__omni_python__functions__']:
                    return GD['__omni_python__functions__']['{hash}']['{name}']({args})
             else:
                 GD['__omni_python__functions__'] = {{}}
             sys.path.insert(0, {site_packages})
             {loader}
             {code}
             sys.meta_path.pop(0)
             GD['__omni_python__functions__']['{hash}'] = locals()
             return {name}({args})
             """).format(hash=hash, code=code,
                         name=name,
                         loader=loader,
                         site_packages=site_packages,
                         args=', '.join(
                             [process_argument(f, a) for a in
                              inspect.getfullargspec(f).args if a != 'self'])))
        for name, f, pgargs in pg_functions]

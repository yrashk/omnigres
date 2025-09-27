#! /usr/bin/env uv run
import os.path
from collections import defaultdict
from itertools import groupby

import click
import docker
import rich
from rich.progress import Progress
from more_itertools import unique

import maintainer
import maintainer.stackgres as sgres
from maintainer.stackgres.models import Publisher, Extension, Version, Target


@click.group()
def main():
    pass


def do_lint_versions():
    project = maintainer.Project()

    def version_gaps():
        """
        Check if any extension has a version gap
        :return:
        """
        for (name, versions) in groupby(sorted(project.extensions(), key=lambda e: e.name),
                                        key=lambda e: e.name):
            sorted_by_versions = sorted(unique(versions, key=lambda e: e.version), key=lambda e: e.version)
            versions = [ext.version for ext in sorted_by_versions]

            major_gaps = []
            minor_gaps = []

            # Group by major.minor
            major_minor_pairs = set((v.major, v.minor) for v in versions)

            # Check each major version's minors
            by_major = defaultdict(list)
            for major, minor in major_minor_pairs:
                by_major[major].append(minor)

            success = True
            for major, minors in by_major.items():
                minors = sorted(minors)
                for i in range(len(minors) - 1):
                    if minors[i + 1] - minors[i] > 1:
                        missing = list(range(minors[i] + 1, minors[i + 1]))
                        minor_gaps.append((major, missing))

            # Check major version gaps
            majors = sorted(by_major.keys())
            for i in range(len(majors) - 1):
                if majors[i + 1] - majors[i] > 1:
                    missing = list(range(majors[i] + 1, majors[i + 1]))
                    major_gaps.append(missing)

            if len(major_gaps) > 0:
                rich.print(f":x: Major gaps in {name}:")
                rich.print(major_gaps)
                success = False

            if len(minor_gaps) > 0:
                rich.print(f":x: Minor gaps in {name}:")
                rich.print(major_gaps)
                success = False

        if success:
            rich.print(f":white_check_mark: No version gaps found")

        return success

    success = version_gaps()
    if not success:
        return 1


@click.group()
def lint():
    pass


@click.command("versions")
def lint_versions():
    do_lint_versions()


lint.add_command(lint_versions)


@click.group()
def stackgres():
    pass


@click.command("genkey")
def generate_publisher_key():
    index_file = os.path.join(os.path.dirname(__file__), "stackgres", "index.json")
    if os.path.exists(index_file):
        rich.print(f":x: Index file {index_file} already exists")
        exit(1)
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.hazmat.primitives import serialization

    # Generate private key
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=4096,
    )

    public_key = private_key.public_key()
    public_pem = public_key.public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )

    # Save to PEM file
    with open('stackgres/publisher_private_key.pem', 'wb') as f:
        f.write(private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption()
        ))
    from maintainer.stackgres import Index
    index = Index(extensions=[], publishers=[
        Publisher(id="com.omnigres", name="Omnigres", email="contact@omnigres.com", url="https://omnigres.com",
                  publicKey=public_pem.decode("ascii"))])
    with open(index_file, "w") as f:
        f.write(index.model_dump_json(indent=2))


@click.command("pubkey")
def get_publisher_key():
    from cryptography.hazmat.primitives import serialization, hashes
    from cryptography.hazmat.primitives.asymmetric.padding import PKCS1v15
    with open('stackgres/publisher_private_key.pem', 'rb') as f:
        private_key = serialization.load_pem_private_key(
            f.read(),
            password=None
        )
    public_key = private_key.public_key()
    # Save to PEM file
    with open('stackgres/publisher_public_key.pem', 'wb') as f:
        f.write(public_key.public_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PublicFormat.PKCS1
        ))

@click.command("package")
@click.argument("image")
def package(image: str):
    from cryptography.hazmat.primitives import serialization, hashes
    from cryptography.hazmat.primitives.asymmetric.padding import PKCS1v15
    with open('stackgres/publisher_private_key.pem', 'rb') as f:
        private_key = serialization.load_pem_private_key(
            f.read(),
            password=None
        )
    index_file = os.path.join(os.path.dirname(__file__), "stackgres", "index.json")
    if not os.path.exists(index_file):
        rich.print(f":x: Index file {index_file} does not exists")
        exit(1)
    from maintainer.stackgres import Index
    import re
    with open(index_file, "r") as f:
        index = Index.model_validate_json(f.read())
    import tarfile
    import io
    with Progress() as progress:
        client = docker.from_env()
        container = client.containers.create(image)
        pg_version = \
            [env.split("=")[1] for env in container.attrs['Config']['Env'] if env.split("=")[0] == "PG_VERSION"][0]
        build = \
            [env.split("=")[1] for env in container.attrs['Config']['Env'] if env.split("=")[0] == "BUILD"][0]
        pg_version_major = pg_version.split(".")[0]
        progress.print(f"Postgres version: {pg_version}")
        bits, stat = container.get_archive('/install')
        copy_task = progress.add_task(f"Downloading exensions", total=stat['size'])
        tar_stream = io.BytesIO()
        for chunk in bits:
            tar_stream.write(chunk)
            progress.advance(copy_task, len(chunk))
        tar_stream.seek(0)
        file_task = progress.add_task("Extracting exensions")
        with tarfile.open(fileobj=tar_stream, mode='r') as tar:
            progress.update(file_task, total=len(tar.getnames()))
            for member in sorted(tar, key=lambda m: m.type):
                progress.advance(file_task, 1)
                shared_library = re.search(r"(omni.*)--(.*)\.so$", member.name)
                control_file = re.search(r"(omni.*)--(.*)\.control$", member.name)
                default_control_file = re.search(r"(omni.*)\.control$", member.name)
                sql_file = re.search(r"(omni.*)--(.*)\.sql$", member.name)
                if shared_library:
                    name = shared_library.group(1)
                    version = shared_library.group(2)
                    # progress.print(f"Shared library {member.name} {name} {version}")
                    build_path = os.path.join("stackgres", "_build", name, version)
                    os.makedirs(build_path, exist_ok=True)
                    lib_dir = os.path.join(build_path, "usr", "lib", "postgresql", pg_version_major, "lib")
                    os.makedirs(lib_dir, exist_ok=True)
                    with open(os.path.join(lib_dir, os.path.basename(member.name)), "wb") as f:
                        f.write(tar.extractfile(member).read())
                elif control_file:
                    name = control_file.group(1)
                    version = control_file.group(2)
                    # progress.print(f"Control file {member.name} {name} {version}")
                    build_path = os.path.join("stackgres", "_build", name, version)
                    os.makedirs(build_path, exist_ok=True)
                    ext_dir = os.path.join(build_path, "usr", "share", "postgresql", pg_version_major, "extension")
                    os.makedirs(ext_dir, exist_ok=True)
                    with open(os.path.join(ext_dir, os.path.basename(member.name)), "wb") as f:
                        f.write(tar.extractfile(member).read())
                elif default_control_file:
                    name = default_control_file.group(1)
                    control = tar.extractfile(member).read().decode("utf-8")
                    version = re.search(r"default_version\s*=\s*['\"]([^'\"]+)['\"]", control).group(1)
                    build_path = os.path.join("stackgres", "_build", name, version)
                    os.makedirs(build_path, exist_ok=True)
                    ext_dir = os.path.join(build_path, "usr", "share", "postgresql", pg_version_major, "extension")
                    os.makedirs(ext_dir, exist_ok=True)
                    with open(os.path.join(ext_dir, os.path.basename(member.name)), "wb") as f:
                        f.write(tar.extractfile(member).read())
                elif sql_file:
                    name = sql_file.group(1)
                    version = sql_file.group(2)
                    # progress.print(f"SQL file {member.name}")
                    build_path = os.path.join("stackgres", "_build", name, version)
                    os.makedirs(build_path, exist_ok=True)
                    ext_dir = os.path.join(build_path, "usr", "share", "postgresql", pg_version_major, "extension")
                    os.makedirs(ext_dir, exist_ok=True)
                    with open(os.path.join(ext_dir, os.path.basename(member.name)), "wb") as f:
                        f.write(tar.extractfile(member).read())
                elif member.isdir():
                    # Ignore dirs
                    pass
                elif member.issym() or member.islnk():
                    # FIXME: this is convoluted, and exists (so far) just to accommodate omni.so
                    name, _ext = os.path.splitext(os.path.basename(member.name))
                    version = \
                        [e for e in os.listdir(os.path.join("stackgres", "_build", name)) if not e.startswith(".")][
                            0]
                    path = member.name.replace("install/", "")
                    path = path.replace(pg_version, pg_version_major)
                    build_path = os.path.join("stackgres", "_build", name, version)
                    linkpath = os.path.join(build_path, path)
                    if os.path.exists(linkpath):
                        os.unlink(linkpath)
                    os.symlink(member.linkname, linkpath)
                else:
                    progress.print(f"Unknown file {member}")
        # Process what we found in the tar
        build_path = os.path.join("stackgres", "_build")
        ext_versions = 0
        for ext in [dir for dir in os.listdir(build_path) if not dir.startswith(".")]:
            for ver in [dir for dir in os.listdir(os.path.join(build_path, ext)) if not dir.startswith(".")]:
                ext_versions += 1
        package_task = progress.add_task("Archiving packages", total=ext_versions)
        for ext in [dir for dir in os.listdir(build_path) if not dir.startswith(".")]:
            for ver in [dir for dir in os.listdir(os.path.join(build_path, ext)) if not dir.startswith(".")]:
                extensions = [extension for extension in index.extensions if extension.name == ext]
                if len(extensions) == 0:
                    extension = Extension(name=ext, publisher="com.omnigres", license="Apache-2.0", channels={},
                                          abstract="", description="", tags=[], versions=[],
                                          source="https://github.com/omnigres/omnigres",
                                          url="https://omnigres.com")
                    index.extensions.append(extension)
                else:
                    extension = extensions[0]
                extension.source = "https://github.com/omnigres/omnigres"
                extension.url = "https://omnigres.com"
                versions = [version for version in extension.versions if version.version == ver]
                version: Version
                if len(versions) == 0:
                    version = Version(version=ver, availableFor=[])
                    extension.versions.append(version)
                else:
                    version = versions[0]
                archs = {"arm64": "aarch64", "amd64": "x86_64"}
                arch = archs[container.image.attrs['Architecture']]
                targets = [target for target in version.availableFor if
                           target.arch == arch and target.build == build and target.postgresVersion == pg_version_major and target.postgresExactVersion == pg_version]
                if len(targets) == 0:
                    target = Target(flavor="pg",
                                    arch=arch,
                                    build=build,
                                    postgresVersion=pg_version_major,
                                    postgresExactVersion=pg_version)
                    version.availableFor.append(target)
                    progress.print(f"Added {target}")
                else:
                    target = targets[0]
                    progress.print(f"Keep {target}")

                def read_control_file(ext, ver=None):
                    requires_list = []
                    if not os.path.exists(os.path.join(build_path, ext)):
                        return requires_list
                    if ver is None:
                        ver = [dir for dir in os.listdir(os.path.join(build_path, ext)) if
                               not dir.startswith(".")][0]
                    control_file = os.path.join("stackgres", "_build", ext, ver, "usr", "share",
                                                "postgresql", pg_version_major, "extension",
                                                f"{ext}--{ver}.control")
                    with open(control_file, "r") as f:
                        control_file_content = f.read()
                    requires_match = re.search(r"^requires\s*=\s*'([^']+)'", control_file_content, re.MULTILINE)
                    if requires_match:
                        requires_list = [item.strip() for item in requires_match.group(1).split(',')]
                    for requirement in requires_list:
                        requires_list.extend(read_control_file(requirement))
                    return requires_list

                requires_list = read_control_file(extension.name, version.version)

                target_file_path = os.path.join("stackgres", "_repository", extension.target_file_path(version, target))
                os.makedirs(os.path.dirname(target_file_path), exist_ok=True)
                with tarfile.open(f"{target_file_path}.tgz", "w:gz") as tar:
                    tar.add(os.path.join(build_path, ext, ver), arcname="/")
                    # Add dependencies that are present
                    for requirement in requires_list:
                        if os.path.exists(os.path.join(build_path, requirement)):
                            requirement_ver = \
                                [dir for dir in os.listdir(os.path.join(build_path, requirement)) if
                                 not dir.startswith(".")][0]
                            tar.add(os.path.join(build_path, requirement, requirement_ver), arcname="/")
                with open(f"{target_file_path}.tgz", "rb") as tar:
                    content = tar.read()
                    signature = private_key.sign(data=content, padding=PKCS1v15(), algorithm=hashes.SHA256())
                    with open(f'{target_file_path}.sha256', 'wb') as f:
                        f.write(signature)
                with tarfile.open(f"{target_file_path}.tar", "w") as tar:
                    tar.add(f"{target_file_path}.tgz", arcname=f"{os.path.basename(target_file_path)}.tgz")
                    tar.add(f"{target_file_path}.sha256", arcname=f"{os.path.basename(target_file_path)}.sha256")
                    progress.advance(package_task, 1)
                os.unlink(f"{target_file_path}.tgz")
                os.unlink(f"{target_file_path}.sha256")


        with open("stackgres/index.json", "w") as f:
            f.write(index.model_dump_json(indent=2))


stackgres.add_command(generate_publisher_key)
stackgres.add_command(get_publisher_key)
stackgres.add_command(package)

main.add_command(lint)
main.add_command(stackgres)

if __name__ == "__main__":
    main()

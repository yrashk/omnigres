from functools import cached_property
from typing import List, Optional, Self, Any

from pydantic import BaseModel


class Target(BaseModel):
    flavor: Optional[str] = None
    arch: Optional[str] = "x86_64"
    build: Optional[str] = None
    postgresVersion: str
    postgresExactVersion: Optional[str] = None

    @property
    def build_flavor(self) -> str:
        if self.flavor:
            return self.flavor
        else:
            return "pg"

    @property
    def major_version(self) -> str:
        return self.postgresVersion.split(".")[0]

    @property
    def exact_version(self) -> str:
        return self.postgresExactVersion or self.postgresVersion

    @property
    def build_suffix(self) -> str:
        if self.build:
            return f"-build-{self.build}"
        else:
            return ""


class Version(BaseModel):
    version: str
    availableFor: Optional[List[Target]]


class Extension(BaseModel):
    name: str
    publisher: str
    license: str
    abstract: Optional[str]
    description: Optional[str]
    channels: Optional[dict[str, str]]
    tags: List[str]
    versions: List[Version]
    url: str
    source: str

    def target_file_path(self, version: Version, target: Target, publisher="com.omnigres") -> str:
        return f"{publisher}/{target.arch}/linux/{self.name}-{version.version}-{target.build_flavor}{target.postgresVersion}{target.build_suffix}"


class Publisher(BaseModel):
    name: str
    id: str
    email: str
    url: str
    publicKey: str


class Index(BaseModel):
    extensions: List[Extension]
    publishers: List[Publisher]

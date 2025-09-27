from container_inspector.image import Image
from markdown_it.rules_inline import image

from .models import Index, Extension, Version, Target


class StackgresContainerBuild:
    def __init__(self, image: str):
        self.image = image

    def extract(self, path: str):
        self.image.extract_layers(path)

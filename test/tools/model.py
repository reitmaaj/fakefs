"""Reference model of the fakefs namespace for property-based testing."""


class Content:
    __slots__ = ("data", "refs")

    def __init__(self, data=b""):
        self.data = bytearray(data)
        self.refs = 1


class Model:
    """A minimal POSIX-like tree mirroring the subset of fakefs under test."""

    def __init__(self):
        self.root = {"type": "dir", "entries": {}}

    # --- path helpers (absolute paths only) ---

    @staticmethod
    def _split(path):
        parts = [p for p in path.split("/") if p]
        return parts

    def _lookup(self, path, follow=True):
        """Return (node, parent_dir, name) or raise ValueError on ENOENT."""
        parts = self._split(path)
        if not parts:
            return self.root, None, None
        node = self.root
        for i, part in enumerate(parts):
            if node["type"] != "dir":
                raise ValueError("ENOTDIR")
            if part not in node["entries"]:
                raise ValueError("ENOENT")
            child = node["entries"][part]
            last = i == len(parts) - 1
            if child["type"] == "link" and (follow or not last):
                target = child["target"]
                child = self._resolve_link(target, child)
            if not last:
                node = child
            else:
                return child, node, part
        return node, None, None

    def _resolve_link(self, target, linknode, depth=0):
        if depth > 40:
            raise ValueError("ELOOP")
        parts = self._split(target)
        node = self.root
        for part in parts:
            if node["type"] != "dir":
                raise ValueError("ENOTDIR")
            if part not in node["entries"]:
                raise ValueError("ENOENT")
            node = node["entries"][part]
            if node["type"] == "link":
                node = self._resolve_link(node["target"], node, depth + 1)
        return node

    def _parent(self, path):
        parts = self._split(path)
        node = self.root
        for part in parts[:-1]:
            if node["type"] != "dir" or part not in node["entries"]:
                raise ValueError("ENOENT")
            node = node["entries"][part]
            if node["type"] == "link":
                node = self._resolve_link(node["target"], node)
            if node["type"] != "dir":
                raise ValueError("ENOTDIR")
        return node, parts[-1]

    @staticmethod
    def _nlink_dir(node):
        return 2 + sum(1 for c in node["entries"].values() if c["type"] == "dir")

    # --- operations (return ok, mutate model) ---

    def mkdir(self, path):
        parent, name = self._parent(path)
        if name in parent["entries"]:
            return False
        parent["entries"][name] = {"type": "dir", "entries": {}}
        return True

    def rmdir(self, path):
        node, parent, name = self._lookup(path, follow=False)
        if name is None or parent is None:
            return False
        if node["type"] != "dir":
            return False
        if node["entries"]:
            return False
        del parent["entries"][name]
        return True

    def unlink(self, path):
        node, parent, name = self._lookup(path, follow=False)
        if name is None or parent is None:
            return False
        if node["type"] == "dir":
            return False
        if node["type"] == "file":
            node["content"].refs -= 1
        del parent["entries"][name]
        return True

    def link(self, src, dst):
        srcnode, _, _ = self._lookup(src, follow=False)
        if srcnode["type"] == "dir":
            return False
        parent, name = self._parent(dst)
        if name in parent["entries"]:
            return False
        if srcnode["type"] == "file":
            srcnode["content"].refs += 1
        parent["entries"][name] = srcnode
        return True

    def symlink(self, target, linkpath):
        parent, name = self._parent(linkpath)
        if name in parent["entries"]:
            return False
        parent["entries"][name] = {"type": "link", "target": target}
        return True

    def rename(self, src, dst):
        srcnode, sparent, sname = self._lookup(src, follow=False)
        if sname is None or sparent is None:
            return False
        # reject moving a dir into its own subtree
        if srcnode["type"] == "dir":
            dstparts = self._split(dst)
            sparts = self._split(src)
            if dstparts[: len(sparts)] == sparts and len(dstparts) > len(sparts):
                return False
        try:
            dstnode, dparent, dname = self._lookup(dst, follow=False)
            exists = True
        except ValueError:
            dparent, dname = self._parent(dst)
            exists = False
        if exists:
            if dstnode is srcnode:
                return True
            if srcnode["type"] == "dir" and dstnode["type"] != "dir":
                return False
            if srcnode["type"] != "dir" and dstnode["type"] == "dir":
                return False
            if dstnode["type"] == "dir" and dstnode["entries"]:
                return False
            if dstnode["type"] == "file":
                dstnode["content"].refs -= 1
        del sparent["entries"][sname]
        dparent["entries"][dname] = srcnode
        return True

    def create_file(self, path, data=b""):
        parent, name = self._parent(path)
        if name in parent["entries"]:
            return False
        parent["entries"][name] = {"type": "file", "content": Content(data)}
        return True

    def overwrite_file(self, path, data=b""):
        """open(path, O_WRONLY|O_TRUNC) then write; follows symlinks."""
        try:
            node, _, _ = self._lookup(path, follow=True)
        except ValueError:
            return False
        if node["type"] != "file":
            return False
        node["content"].data = bytearray(data)
        return True

    def append_file(self, path, data=b""):
        try:
            node, _, _ = self._lookup(path, follow=True)
        except ValueError:
            return False
        if node["type"] != "file":
            return False
        node["content"].data.extend(data)
        return True

    def truncate_file(self, path, length):
        try:
            node, _, _ = self._lookup(path, follow=True)
        except ValueError:
            return False
        if node["type"] != "file":
            return False
        data = node["content"].data
        if length < len(data):
            del data[length:]
        else:
            data.extend(b"\0" * (length - len(data)))
        return True

    # --- snapshot for comparison ---

    def snapshot(self):
        out = {}

        def rec(node, path):
            if node["type"] == "dir":
                out[path] = ("dir", self._nlink_dir(node))
                for name in sorted(node["entries"]):
                    rec(node["entries"][name], path + name + "/")
            elif node["type"] == "file":
                out[path.rstrip("/")] = (
                    "file", node["content"].refs, bytes(node["content"].data))
            else:
                out[path.rstrip("/")] = ("link", 1, node["target"])

        rec(self.root, "/")
        return out

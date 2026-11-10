#!/usr/bin/env python3
"""Parse a GoldenEye decomp Model.c (gun/prop) into Python structures."""
import re

NODE_RE = re.compile(r'ModelNode (ModelNode_0x[0-9a-f]+) = \{(MODELNODE_OPCODE_\w+), (&?\w+|NULL), (&?\w+|NULL), (&?\w+|NULL), (&?\w+|NULL), (&?\w+|NULL)\};')
REC_RE = re.compile(r'ModelRoData_(\w+) (\w+) =\s*\{(.*?)\n\};', re.S)
VTX_RE = re.compile(r'Vertex (Vertex_0x[0-9a-f]+)\[\w+\] =\s*\{(.*?)\n\};', re.S)
GFX_RE = re.compile(r'Gfx (\w+)\[\] =\s*\{(.*?)\n\};', re.S)
NUM = r'-?[0-9.]+(?:e-?\d+)?'

def strip(v):
    return None if v == 'NULL' else v.lstrip('&')

def split_args(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '({': depth += 1
        if ch in ')}': depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

class Model:
    def __init__(self, path):
        t = open(path).read()
        self.text = t
        self.nodes = {}
        for m in NODE_RE.finditer(t):
            # GE ModelNode layout: {opcode, data, parent, next sibling, prev sibling, first child}
            name, op, data, parent, nxt, prev, child = m.groups()
            self.nodes[name] = dict(name=name, op=op.replace('MODELNODE_OPCODE_', ''), data=strip(data),
                                    parent=strip(parent), child=strip(child), prev=strip(prev), next=strip(nxt))
        self.records = {}
        for m in REC_RE.finditer(t):
            kind, name, body = m.groups()
            body = re.sub(r'//[^\n]*', '', body)
            self.records[name] = (kind, body)
        self.vertices = {}
        for m in VTX_RE.finditer(t):
            name, body = m.groups()
            rows = re.findall(r'\{\s*\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\},\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+)\s*\}', body)
            self.vertices[name] = [tuple(int(x, 0) for x in r) for r in rows]
        self.gfx = {}
        for m in GFX_RE.finditer(t):
            name, body = m.groups()
            cmds = []
            for line in body.split('\n'):
                line = line.strip().rstrip(',')
                mm = re.match(r'(\w+)\((.*)\)$', line)
                if mm:
                    cmds.append((mm.group(1), split_args(mm.group(2))))
            self.gfx[name] = cmds
        self.textures = re.findall(r'\{(IMAGE_\w+), (\d+), (\d+),', t[t.find('proptextures'):t.find('};', t.find('proptextures'))])
        self.root = next(iter(self.nodes)) if self.nodes else None

    def group(self, name):
        kind, body = self.records[name]
        nums = re.findall(NUM, body)
        origin = tuple(float(x) for x in nums[:3])
        ints = re.findall(r'0x[0-9A-Fa-f]+', body)
        return dict(origin=origin, joint=int(ints[0], 16), matrix=int(ints[1], 16))

    def dlrec(self, name):
        kind, body = self.records[name]
        parts = [p.strip() for p in body.replace('\n', ' ').split(',')]
        refs = re.findall(r'&(\w+)|NULL', body)
        prim = re.search(r'&(GFX_\w+)[^,]*,\s*(?:&(GFX_\w+)|NULL)', body)
        vtx = re.search(r'&(Vertex_0x[0-9a-f]+)', body)
        return dict(primary=prim.group(1) if prim else None, secondary=prim.group(2) if prim else None,
                    vertices=vtx.group(1) if vtx else None)

    def bsp(self, name):
        kind, body = self.records[name]
        kids = re.findall(r'&(ModelNode_0x[0-9a-f]+)|NULL', body)
        return [k for k in kids if k]

    def switch(self, name):
        kind, body = self.records[name]
        c = re.search(r'&(ModelNode_0x[0-9a-f]+)', body)
        return c.group(1) if c else None

    def walk(self):
        """Yield (node, depth, matrixPos dict, switchStack) over the whole tree."""
        seen = set()
        out = []
        def visit(n, depth, switches):
            if n is None or n in seen:
                return
            seen.add(n)
            node = self.nodes[n]
            sw = switches
            if node['op'] == 'SWITCH':
                sw = switches + [node['data']]
            out.append((node, depth, switches))
            if node['op'] == 'BSP':
                for k in self.bsp(node['data']):
                    visit(k, depth + 1, sw)
            c = node['child']
            while c is not None:
                visit(c, depth + 1, sw)
                c = self.nodes[c]['next']
        visit(self.root, 0, [])
        return out

    def matrix_positions(self):
        """Accumulate group origins down the tree: MatrixID -> absolute position."""
        pos = {}
        def visit(n, base):
            if n is None:
                return
            node = self.nodes[n]
            here = base
            if node['op'] == 'GROUP':
                g = self.group(node['data'])
                here = tuple(base[i] + g['origin'][i] for i in range(3))
                if g['matrix'] != 0xFFFF:
                    pos[g['matrix']] = here
            kids = []
            if node['op'] == 'BSP':
                kids += self.bsp(node['data'])
            c = node['child']
            while c is not None:
                kids.append(c)
                c = self.nodes[c]['next']
            for k in kids:
                visit(k, here)
        visit(self.root, (0.0, 0.0, 0.0))
        return pos

// Forwards the engine's OpenGL ES 2.0 calls to the real driver.
//
// Arguments arrive soft-float (floats in core registers). Guest pointers are
// translated to host pointers (guest memory is host memory at base + g), so the
// driver can read client-side vertex arrays and texture data directly.
#include <GLES2/gl2.h>

#include <map>
#include <set>
#include <vector>

#include "engine.h"

namespace nin {
namespace {

struct GlState {
  GLuint arrayBuffer = 0;           // bound GL_ARRAY_BUFFER (pointer vs offset)
  std::map<GLenum, uint32_t> strings;  // glGetString results copied into guest memory
  // Objects the engine created and has not deleted, released when it shuts
  // down: the EGL context outlives the engine (see NINView.ContextFactory).
  std::set<GLuint> buffers, framebuffers, renderbuffers, textures, programs, shaders;
};

void track(std::set<GLuint>& set, GLsizei n, const GLuint* names, bool add) {
  for (GLsizei i = 0; names && i < n; i++) {
    if (add) set.insert(names[i]);
    else set.erase(names[i]);
  }
}

void deleteAll(std::set<GLuint>& set, void (*del)(GLsizei, const GLuint*)) {
  std::vector<GLuint> names(set.begin(), set.end());
  if (!names.empty()) del(GLsizei(names.size()), names.data());
  set.clear();
}

}  // namespace

void installGles(Engine& e) {
  auto* st = new GlState;
  e.onShutdown([st] {
    deleteAll(st->buffers, glDeleteBuffers);
    deleteAll(st->framebuffers, glDeleteFramebuffers);
    deleteAll(st->renderbuffers, glDeleteRenderbuffers);
    deleteAll(st->textures, glDeleteTextures);
    for (GLuint p : st->programs) glDeleteProgram(p);
    for (GLuint s : st->shaders) glDeleteShader(s);
    delete st;
  });
  Guest& g = e.guest();
  auto def = [&](const char* name, Thunk t) {
    g.setThunk(name, std::move(t));
  };
  auto ptr = [](Guest& g, uint32_t p) -> void* { return p ? g.host(p) : nullptr; };

#define V1(fn, T1) def(#fn, [](Guest& g, Args& a) { T1 x = T1(a.u32()); fn(x); })
#define V2(fn, T1, T2) def(#fn, [](Guest& g, Args& a) { T1 x = T1(a.u32()); T2 y = T2(a.u32()); fn(x, y); })
#define V3(fn, T1, T2, T3) def(#fn, [](Guest& g, Args& a) { T1 x = T1(a.u32()); T2 y = T2(a.u32()); T3 z = T3(a.u32()); fn(x, y, z); })
#define V4(fn, T1, T2, T3, T4) def(#fn, [](Guest& g, Args& a) { T1 x = T1(a.u32()); T2 y = T2(a.u32()); T3 z = T3(a.u32()); T4 w = T4(a.u32()); fn(x, y, z, w); })

  V1(glActiveTexture, GLenum);
  V2(glAttachShader, GLuint, GLuint);
  def("glBindAttribLocation", [](Guest& g, Args& a) {
    GLuint p = a.u32(), i = a.u32();
    glBindAttribLocation(p, i, g.cstr(a.u32()));
  });
  def("glBindBuffer", [st](Guest& g, Args& a) {
    GLenum target = a.u32();
    GLuint buffer = a.u32();
    if (target == GL_ARRAY_BUFFER) st->arrayBuffer = buffer;
    glBindBuffer(target, buffer);
  });
  V2(glBindFramebuffer, GLenum, GLuint);
  V2(glBindRenderbuffer, GLenum, GLuint);
  V2(glBindTexture, GLenum, GLuint);
  V2(glBlendFunc, GLenum, GLenum);
  V4(glBlendFuncSeparate, GLenum, GLenum, GLenum, GLenum);
  def("glBufferData", [ptr](Guest& g, Args& a) {
    GLenum target = a.u32();
    GLsizeiptr size = GLsizeiptr(a.i32());
    void* data = ptr(g, a.u32());
    GLenum usage = a.u32();
    glBufferData(target, size, data, usage);
  });
  def("glCheckFramebufferStatus", [](Guest& g, Args& a) { g.ret32(glCheckFramebufferStatus(a.u32())); });
  V1(glClear, GLbitfield);
  def("glClearColor", [](Guest& g, Args& a) {
    float r = a.f32(), gg = a.f32(), b = a.f32(), al = a.f32();
    glClearColor(r, gg, b, al);
  });
  def("glClearDepthf", [](Guest& g, Args& a) { glClearDepthf(a.f32()); });
  V1(glClearStencil, GLint);
  V4(glColorMask, GLboolean, GLboolean, GLboolean, GLboolean);
  V1(glCompileShader, GLuint);
  def("glCopyTexImage2D", [](Guest& g, Args& a) {
    GLenum t = a.u32();
    GLint l = a.i32();
    GLenum f = a.u32();
    GLint x = a.i32(), y = a.i32();
    GLsizei w = a.i32(), h = a.i32();
    GLint border = a.i32();
    glCopyTexImage2D(t, l, f, x, y, w, h, border);
  });
  def("glCreateProgram", [st](Guest& g, Args&) {
    GLuint p = glCreateProgram();
    if (p) st->programs.insert(p);
    g.ret32(p);
  });
  def("glCreateShader", [st](Guest& g, Args& a) {
    GLuint s = glCreateShader(a.u32());
    if (s) st->shaders.insert(s);
    g.ret32(s);
  });
  V1(glCullFace, GLenum);
#define DEL(fn, set) def(#fn, [st](Guest& g, Args& a) { \
    GLsizei n = a.i32(); const GLuint* names = g.hp<GLuint>(a.u32()); track(st->set, n, names, false); fn(n, names); })
  DEL(glDeleteBuffers, buffers);
  DEL(glDeleteFramebuffers, framebuffers);
  DEL(glDeleteTextures, textures);
#undef DEL
  def("glDeleteProgram", [st](Guest& g, Args& a) {
    GLuint p = a.u32();
    st->programs.erase(p);
    glDeleteProgram(p);
  });
  def("glDeleteShader", [st](Guest& g, Args& a) {
    GLuint s = a.u32();
    st->shaders.erase(s);
    glDeleteShader(s);
  });
  V1(glDepthFunc, GLenum);
  V1(glDepthMask, GLboolean);
  def("glDepthRangef", [](Guest& g, Args& a) {
    float n = a.f32(), f = a.f32();
    glDepthRangef(n, f);
  });
  V1(glDisable, GLenum);
  V1(glDisableVertexAttribArray, GLuint);
  V3(glDrawArrays, GLenum, GLint, GLsizei);
  V1(glEnable, GLenum);
  V1(glEnableVertexAttribArray, GLuint);
  V4(glFramebufferRenderbuffer, GLenum, GLenum, GLenum, GLuint);
  def("glFramebufferTexture2D", [](Guest& g, Args& a) {
    GLenum t = a.u32(), att = a.u32(), tt = a.u32();
    GLuint tex = a.u32();
    GLint level = a.i32();
    glFramebufferTexture2D(t, att, tt, tex, level);
  });
  V1(glFrontFace, GLenum);
#define GEN(fn, set) def(#fn, [st](Guest& g, Args& a) { \
    GLsizei n = a.i32(); GLuint* names = g.hp<GLuint>(a.u32()); fn(n, names); track(st->set, n, names, true); })
  GEN(glGenBuffers, buffers);
  GEN(glGenFramebuffers, framebuffers);
  GEN(glGenRenderbuffers, renderbuffers);
  GEN(glGenTextures, textures);
#undef GEN
  V1(glGenerateMipmap, GLenum);
#define ACTIVE(fn) def(#fn, [ptr](Guest& g, Args& a) { \
    GLuint p = a.u32(), i = a.u32(); GLsizei bs = a.i32(); \
    GLsizei* len = static_cast<GLsizei*>(ptr(g, a.u32())); GLint* size = static_cast<GLint*>(ptr(g, a.u32())); \
    GLenum* type = static_cast<GLenum*>(ptr(g, a.u32())); GLchar* name = static_cast<GLchar*>(ptr(g, a.u32())); \
    fn(p, i, bs, len, size, type, name); })
  ACTIVE(glGetActiveAttrib);
  ACTIVE(glGetActiveUniform);
#undef ACTIVE
  def("glGetAttribLocation", [](Guest& g, Args& a) {
    GLuint p = a.u32();
    g.ret32(uint32_t(glGetAttribLocation(p, g.cstr(a.u32()))));
  });
  def("glGetUniformLocation", [](Guest& g, Args& a) {
    GLuint p = a.u32();
    g.ret32(uint32_t(glGetUniformLocation(p, g.cstr(a.u32()))));
  });
  def("glGetError", [](Guest& g, Args&) { g.ret32(glGetError()); });
  def("glGetIntegerv", [](Guest& g, Args& a) {
    GLenum pname = a.u32();
    glGetIntegerv(pname, g.hp<GLint>(a.u32()));
  });
#define INFOLOG(fn) def(#fn, [ptr](Guest& g, Args& a) { \
    GLuint o = a.u32(); GLsizei bs = a.i32(); \
    GLsizei* len = static_cast<GLsizei*>(ptr(g, a.u32())); GLchar* log = static_cast<GLchar*>(ptr(g, a.u32())); \
    fn(o, bs, len, log); })
  INFOLOG(glGetProgramInfoLog);
  INFOLOG(glGetShaderInfoLog);
#undef INFOLOG
  def("glGetProgramiv", [](Guest& g, Args& a) {
    GLuint p = a.u32();
    GLenum pname = a.u32();
    glGetProgramiv(p, pname, g.hp<GLint>(a.u32()));
  });
  def("glGetShaderiv", [](Guest& g, Args& a) {
    GLuint s = a.u32();
    GLenum pname = a.u32();
    glGetShaderiv(s, pname, g.hp<GLint>(a.u32()));
  });
  def("glGetString", [st](Guest& g, Args& a) {
    GLenum name = a.u32();
    auto it = st->strings.find(name);
    if (it == st->strings.end()) {
      const GLubyte* s = glGetString(name);
      uint32_t copy = g.allocCString(s ? reinterpret_cast<const char*>(s) : "");
      it = st->strings.emplace(name, copy).first;
    }
    g.ret32(it->second);
  });
  def("glIsEnabled", [](Guest& g, Args& a) { g.ret32(glIsEnabled(a.u32())); });
  def("glLineWidth", [](Guest& g, Args& a) { glLineWidth(a.f32()); });
  V1(glLinkProgram, GLuint);
  def("glReadPixels", [ptr](Guest& g, Args& a) {
    GLint x = a.i32(), y = a.i32();
    GLsizei w = a.i32(), h = a.i32();
    GLenum f = a.u32(), t = a.u32();
    glReadPixels(x, y, w, h, f, t, ptr(g, a.u32()));
  });
  V4(glRenderbufferStorage, GLenum, GLenum, GLsizei, GLsizei);
  V4(glScissor, GLint, GLint, GLsizei, GLsizei);
  def("glShaderSource", [](Guest& g, Args& a) {
    GLuint shader = a.u32();
    GLsizei count = a.i32();
    uint32_t strings = a.u32(), lengths = a.u32();
    std::vector<const GLchar*> s(size_t(std::max(count, 0)));
    std::vector<GLint> l(s.size());
    for (GLsizei i = 0; i < count; i++) {
      s[size_t(i)] = g.cstr(g.r32(strings + 4 * uint32_t(i)));
      l[size_t(i)] = lengths ? GLint(g.r32(lengths + 4 * uint32_t(i))) : -1;
    }
    glShaderSource(shader, count, s.data(), l.data());
  });
  V3(glStencilFunc, GLenum, GLint, GLuint);
  V1(glStencilMask, GLuint);
  V3(glStencilOp, GLenum, GLenum, GLenum);
  def("glTexImage2D", [ptr](Guest& g, Args& a) {
    GLenum t = a.u32();
    GLint level = a.i32(), internal = a.i32();
    GLsizei w = a.i32(), h = a.i32();
    GLint border = a.i32();
    GLenum f = a.u32(), type = a.u32();
    glTexImage2D(t, level, internal, w, h, border, f, type, ptr(g, a.u32()));
  });
  V3(glTexParameteri, GLenum, GLenum, GLint);
  def("glTexSubImage2D", [ptr](Guest& g, Args& a) {
    GLenum t = a.u32();
    GLint level = a.i32(), x = a.i32(), y = a.i32();
    GLsizei w = a.i32(), h = a.i32();
    GLenum f = a.u32(), type = a.u32();
    glTexSubImage2D(t, level, x, y, w, h, f, type, ptr(g, a.u32()));
  });
  def("glUniform1f", [](Guest& g, Args& a) {
    GLint l = a.i32();
    glUniform1f(l, a.f32());
  });
  V2(glUniform1i, GLint, GLint);
  def("glUniform2f", [](Guest& g, Args& a) {
    GLint l = a.i32();
    float x = a.f32(), y = a.f32();
    glUniform2f(l, x, y);
  });
  def("glUniform3f", [](Guest& g, Args& a) {
    GLint l = a.i32();
    float x = a.f32(), y = a.f32(), z = a.f32();
    glUniform3f(l, x, y, z);
  });
  def("glUniform4f", [](Guest& g, Args& a) {
    GLint l = a.i32();
    float x = a.f32(), y = a.f32(), z = a.f32(), w = a.f32();
    glUniform4f(l, x, y, z, w);
  });
  def("glUniform4fv", [](Guest& g, Args& a) {
    GLint l = a.i32();
    GLsizei n = a.i32();
    glUniform4fv(l, n, g.hp<GLfloat>(a.u32()));
  });
  def("glUniformMatrix4fv", [](Guest& g, Args& a) {
    GLint l = a.i32();
    GLsizei n = a.i32();
    GLboolean transpose = GLboolean(a.u32());
    glUniformMatrix4fv(l, n, transpose, g.hp<GLfloat>(a.u32()));
  });
  V1(glUseProgram, GLuint);
  def("glVertexAttribPointer", [st](Guest& g, Args& a) {
    GLuint index = a.u32();
    GLint size = a.i32();
    GLenum type = a.u32();
    GLboolean norm = GLboolean(a.u32());
    GLsizei stride = a.i32();
    uint32_t p = a.u32();
    // With a bound VBO the "pointer" is a byte offset; otherwise guest memory.
    const void* hp = st->arrayBuffer ? reinterpret_cast<const void*>(uintptr_t(p)) : (p ? g.host(p) : nullptr);
    glVertexAttribPointer(index, size, type, norm, stride, hp);
  });
  V4(glViewport, GLint, GLint, GLsizei, GLsizei);
#undef V1
#undef V2
#undef V3
#undef V4
}

}  // namespace nin

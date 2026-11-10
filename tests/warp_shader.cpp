#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

static GLuint compile(GLenum type, const std::string &source) {
  const char *text = source.c_str();
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &text, nullptr); glCompileShader(shader);
  GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char message[4096]{}; glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
    glDeleteShader(shader); throw std::runtime_error(message);
  }
  return shader;
}
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  auto get = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (!get) return 77;
  EGLDisplay display = get(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  if (!eglInitialize(display, nullptr, nullptr)) return 77;
  eglBindAPI(EGL_OPENGL_API);
  const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                         EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig config{}; EGLint count = 0;
  if (!eglChooseConfig(display, attrs, &config, 1, &count) || count == 0) { eglTerminate(display); return 77; }
  const EGLint contextAttrs[] = {EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,
                                EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
  const EGLint size[] = {EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
  EGLSurface surface = eglCreatePbufferSurface(display, config, size);
  if (!eglMakeCurrent(display, surface, surface, context)) {
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display,surface);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display,context);
    eglTerminate(display); return 77;
  }
  int result = 0;
  try {
    std::ifstream input(argv[1]);
    const std::string fragment{std::istreambuf_iterator<char>(input), {}};
    if (fragment.empty()) throw std::runtime_error("missing extracted shader");
    const auto vs = compile(GL_VERTEX_SHADER, R"(#version 330
out vec2 qt_TexCoord0;
void main() {
  vec2 p=vec2((gl_VertexID & 1)*2-1, (gl_VertexID >> 1)*2-1);
  qt_TexCoord0=p*0.5+0.5; gl_Position=vec4(p,0,1);
})");
    const auto fs = compile(GL_FRAGMENT_SHADER, fragment);
    const auto program = glCreateProgram();
    glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
    GLint linked = 0; glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if (!linked) throw std::runtime_error("warp shader link failed");
    glUseProgram(program);
    glUniform1f(glGetUniformLocation(program,"u.qt_Opacity"),0.75f);
    glUniform4f(glGetUniformLocation(program,"u.tint"),0.1f,0.8f,0.3f,1);
    glUniform2f(glGetUniformLocation(program,"u.texelStep"),1.f/32,1.f/32);
    glUniform1f(glGetUniformLocation(program,"u.intensity"),0.28f);
    glUniform1f(glGetUniformLocation(program,"u.motion"),0.9f);
    glUniform1i(glGetUniformLocation(program,"sourceTexture"),0);
    GLuint vao, texture; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    std::array<unsigned char,32*32*4> source{}, pixels{};
    for (int pass=0;pass<2;++pass) {
      if (pass) for (int y=12;y<20;++y) for (int x=15;x<17;++x)
        for (int c=0;c<4;++c) source[4*(y*32+x)+c]=255;
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,source.data());
      glViewport(0,0,32,32); glDrawArrays(GL_TRIANGLE_STRIP,0,4);
      glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
      int lit=0, blurred=0;
      for (size_t i=0;i<pixels.size();i+=4) {
        const int a=pixels[i+3];
        if (a>77 || pixels[i]>a || pixels[i+1]>a || pixels[i+2]>a)
          throw std::runtime_error("non-premultiplied or excessive glow alpha");
        if (a) { ++lit; if (!source[i+3]) ++blurred; }
      }
      if ((!pass && lit) || (pass && (!lit || !blurred)) || pixels[3] != 0)
        throw std::runtime_error("blur/transparent background check failed");
    }
    if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("OpenGL error");
    std::cout << "PASS actual baked warp blur: " << glGetString(GL_RENDERER)
              << "; blur spreads, empty input stays transparent, alpha is bounded\n";
    glDeleteTextures(1,&texture); glDeleteVertexArrays(1,&vao);
    glDeleteProgram(program); glDeleteShader(vs); glDeleteShader(fs);
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; result=1; }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroySurface(display,surface); eglDestroyContext(display,context); eglTerminate(display);
  return result;
}

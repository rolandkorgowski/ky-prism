#define GL_GLEXT_PROTOTYPES
#include "shaders.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <iostream>
#include <stdexcept>
static GLuint compile(GLenum type,const char*source){GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok)throw std::runtime_error("shader compilation");return shader;}
int main(){
 auto get=reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
 if(!get)return 77;
 EGLDisplay display=get(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);
 if(!eglInitialize(display,nullptr,nullptr))return 77;
 eglBindAPI(EGL_OPENGL_API);
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};EGLConfig config;EGLint count;
 if(!eglChooseConfig(display,attrs,&config,1,&count)||count==0)return 77;
 EGLint ctxAttrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxAttrs);
 EGLint size[]={EGL_WIDTH,2,EGL_HEIGHT,2,EGL_NONE};EGLSurface surface=eglCreatePbufferSurface(display,config,size);
 if(!eglMakeCurrent(display,surface,surface,context))return 77;
 int rc=0;
 try{
 GLuint vs=compile(GL_VERTEX_SHADER,PrismShaders::vertex),fs=compile(GL_FRAGMENT_SHADER,PrismShaders::fragment);
 GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)throw std::runtime_error("link");glUseProgram(program);glUniform1i(glGetUniformLocation(program,"photo"),0);
 GLuint vao,vbo,tex;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
 const float points[]={-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1};glBufferData(GL_ARRAY_BUFFER,sizeof(points),points,GL_STATIC_DRAW);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,16,nullptr);glEnableVertexAttribArray(0);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,16,reinterpret_cast<void*>(8));glEnableVertexAttribArray(1);
 glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 const unsigned short pixels[]={65535,0,0,65535, 0,65535,0,65535, 0,0,65535,65535, 32768,32768,32768,65535};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16,2,2,0,GL_RGBA,GL_UNSIGNED_SHORT,pixels);glViewport(0,0,2,2);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
 unsigned char result[16];glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,result);
 if(result[0]!=255 || result[1]!=0 || result[5]!=255 || result[10]!=255 || result[12]<127 || result[12]>129 || glGetError()!=GL_NO_ERROR)throw std::runtime_error("RGBA16 color/order mismatch");
 std::cout<<"PASS GPU pipeline: "<<glGetString(GL_RENDERER)<<"; 16-bit upload, shader, color and orientation\n";
 glDeleteTextures(1,&tex);glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';rc=1;}
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);return rc;
}

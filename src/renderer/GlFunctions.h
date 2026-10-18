#pragma once

// OpenGL entry points used by the renderer.
//
// Desktop builds (GL 3.3 core) load the functions at runtime through
// SDL_GL_GetProcAddress. GLES builds (web) link them directly from
// the GLES3 headers, so the loader is a no-op there.

#if defined(CHI_GLES)
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>

#define CHI_GL_FUNCTIONS(X) \
    X(void, glGenVertexArrays, (GLsizei n, GLuint* arrays)) \
    X(void, glBindVertexArray, (GLuint array)) \
    X(void, glGenBuffers, (GLsizei n, GLuint* buffers)) \
    X(void, glBindBuffer, (GLenum target, GLuint buffer)) \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage)) \
    X(GLuint, glCreateShader, (GLenum type)) \
    X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)) \
    X(void, glCompileShader, (GLuint shader)) \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params)) \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
    X(void, glDeleteShader, (GLuint shader)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint program, GLuint shader)) \
    X(void, glBindAttribLocation, (GLuint program, GLuint index, const GLchar* name)) \
    X(void, glLinkProgram, (GLuint program)) \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params)) \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
    X(void, glUseProgram, (GLuint program)) \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar* name)) \
    X(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)) \
    X(void, glUniform4fv, (GLint location, GLsizei count, const GLfloat* value)) \
    X(void, glEnableVertexAttribArray, (GLuint index)) \
    X(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer)) \
    X(void, glDeleteProgram, (GLuint program)) \
    X(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices)) \
    X(void, glActiveTexture, (GLenum texture)) \
    X(void, glGenerateMipmap, (GLenum target)) \
    X(void, glUniform1i, (GLint location, GLint v0)) \
    X(void, glUniform1f, (GLint location, GLfloat v0)) \
    X(void, glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2)) \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint* arrays)) \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers)) \
    X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data)) \
    X(void, glVertexAttribDivisor, (GLuint index, GLuint divisor)) \
    X(void, glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instancecount)) \
    X(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1)) \
    X(void, glUniform3fv, (GLint location, GLsizei count, const GLfloat* value)) \
    X(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)) \
    X(void, glGenFramebuffers, (GLsizei n, GLuint* framebuffers)) \
    X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers)) \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint* renderbuffers)) \
    X(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer)) \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* renderbuffers)) \
    X(void, glRenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)) \
    X(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter))

#define CHI_GL_DECLARE(ret, name, args) extern ret(APIENTRY* chi_##name) args;
CHI_GL_FUNCTIONS(CHI_GL_DECLARE)
#undef CHI_GL_DECLARE

#endif

// Resolves every function above. Returns false if any is missing.
bool loadGlFunctions();

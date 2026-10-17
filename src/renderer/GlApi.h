#pragma once

// Short GL names for the renderer. Kept out of GlFunctions.h so the
// loader itself does not see them.

#include "renderer/GlFunctions.h"

#if !defined(CHI_GLES)
#define glGenVertexArrays chi_glGenVertexArrays
#define glBindVertexArray chi_glBindVertexArray
#define glGenBuffers chi_glGenBuffers
#define glBindBuffer chi_glBindBuffer
#define glBufferData chi_glBufferData
#define glCreateShader chi_glCreateShader
#define glShaderSource chi_glShaderSource
#define glCompileShader chi_glCompileShader
#define glGetShaderiv chi_glGetShaderiv
#define glGetShaderInfoLog chi_glGetShaderInfoLog
#define glDeleteShader chi_glDeleteShader
#define glCreateProgram chi_glCreateProgram
#define glAttachShader chi_glAttachShader
#define glBindAttribLocation chi_glBindAttribLocation
#define glLinkProgram chi_glLinkProgram
#define glGetProgramiv chi_glGetProgramiv
#define glGetProgramInfoLog chi_glGetProgramInfoLog
#define glUseProgram chi_glUseProgram
#define glGetUniformLocation chi_glGetUniformLocation
#define glUniformMatrix4fv chi_glUniformMatrix4fv
#define glUniform4fv chi_glUniform4fv
#define glEnableVertexAttribArray chi_glEnableVertexAttribArray
#define glVertexAttribPointer chi_glVertexAttribPointer
#define glDeleteProgram chi_glDeleteProgram
#define glDrawElements chi_glDrawElements
#define glActiveTexture chi_glActiveTexture
#define glGenerateMipmap chi_glGenerateMipmap
#define glUniform1i chi_glUniform1i
#define glUniform1f chi_glUniform1f
#define glDeleteVertexArrays chi_glDeleteVertexArrays
#define glDeleteBuffers chi_glDeleteBuffers
#endif

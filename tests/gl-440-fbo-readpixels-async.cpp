#include "test.hpp"

namespace
{
	char const * VERT_SHADER_SOURCE_TEXTURE("gl-440/fbo-readpixels-sample.vert");
	char const * FRAG_SHADER_SOURCE_TEXTURE("gl-440/fbo-readpixels-sample.frag");
	char const * VERT_SHADER_SOURCE_SPLASH("gl-440/fbo-readpixels-blit.vert");
	char const * FRAG_SHADER_SOURCE_SPLASH("gl-440/fbo-readpixels-blit.frag");
	char const * TEXTURE_DIFFUSE("kueken7_rgba8_srgb.dds");

	GLsizei const VertexCount(4);
	GLsizeiptr const VertexSize = VertexCount * sizeof(glf::vertex_v2fv2f);
	glf::vertex_v2fv2f const VertexData[VertexCount] =
	{
		glf::vertex_v2fv2f(glm::vec2(-1.0f,-1.0f), glm::vec2(0.0f, 1.0f)),
		glf::vertex_v2fv2f(glm::vec2( 1.0f,-1.0f), glm::vec2(1.0f, 1.0f)),
		glf::vertex_v2fv2f(glm::vec2( 1.0f, 1.0f), glm::vec2(1.0f, 0.0f)),
		glf::vertex_v2fv2f(glm::vec2(-1.0f, 1.0f), glm::vec2(0.0f, 0.0f))
	};

	GLsizei const ElementCount(6);
	GLsizeiptr const ElementSize = ElementCount * sizeof(GLushort);
	GLushort const ElementData[ElementCount] =
	{
		0, 1, 2, 
		2, 3, 0
	};

	namespace buffer
	{
		enum type
		{
			VERTEX,
			ELEMENT,
			TRANSFORM,
			MAX
		};
	}//namespace buffer

	namespace texture
	{
		enum type
		{
			DIFFUSE,
			COLORBUFFER,
			RENDERBUFFER,
			MAX
		};
	}//namespace texture
	
	namespace program
	{
		enum type
		{
			TEXTURE,
			SPLASH,
			MAX
		};
	}//namespace program

	namespace shader
	{
		enum type
		{
			VERT_TEXTURE,
			FRAG_TEXTURE,
			VERT_SPLASH,
			FRAG_SPLASH,
			MAX
		};
	}//namespace shader

	struct transfer
	{
		GLuint Buffer;
		GLuint Stagging;
		GLsync Fence;
	};
}//namespace

class gl_440_fbo_readpixels_async : public test
{
public:
	gl_440_fbo_readpixels_async(int argc, char* argv[]) :
		test(argc, argv, "gl-440-fbo-readpixels-async", test::CORE, 4, 3),
		FramebufferName(0),
		FramebufferScale(2),
		UniformTransform(-1)
	{}

private:
	std::array<GLuint, program::MAX> ProgramName;
	std::array<GLuint, program::MAX> VertexArrayName;
	std::array<GLuint, buffer::MAX> BufferName;
	std::array<GLuint, texture::MAX> TextureName;
	std::array<GLint, program::MAX> UniformDiffuse;
	GLuint FramebufferName;
	glm::uint FramebufferScale;
	GLint UniformTransform;
	std::vector<glm::uint> ReadPixelData;
	std::queue<transfer*> ReadPixelBufferLive;
	std::queue<transfer*> ReadPixelBufferFree;

	bool initProgram()
	{
		bool Validated(true);

		compiler Compiler;

		std::array<GLuint, shader::MAX> ShaderName;

		if(Validated)
		{
			ShaderName[shader::VERT_TEXTURE] = Compiler.create(GL_VERTEX_SHADER, getDataDirectory() + VERT_SHADER_SOURCE_TEXTURE, "--version 150 --profile core");
			ShaderName[shader::FRAG_TEXTURE] = Compiler.create(GL_FRAGMENT_SHADER, getDataDirectory() + FRAG_SHADER_SOURCE_TEXTURE, "--version 150 --profile core");

			ProgramName[program::TEXTURE] = glCreateProgram();
			glAttachShader(ProgramName[program::TEXTURE], ShaderName[shader::VERT_TEXTURE]);
			glAttachShader(ProgramName[program::TEXTURE], ShaderName[shader::FRAG_TEXTURE]);

			glBindAttribLocation(ProgramName[program::TEXTURE], semantic::attr::POSITION, "Position");
			glBindAttribLocation(ProgramName[program::TEXTURE], semantic::attr::TEXCOORD, "Texcoord");
			glBindFragDataLocation(ProgramName[program::TEXTURE], semantic::frag::COLOR, "Color");
			glLinkProgram(ProgramName[program::TEXTURE]);
		}

		if(Validated)
		{
			ShaderName[shader::VERT_SPLASH] = Compiler.create(GL_VERTEX_SHADER, getDataDirectory() + VERT_SHADER_SOURCE_SPLASH, "--version 150 --profile core");
			ShaderName[shader::FRAG_SPLASH] = Compiler.create(GL_FRAGMENT_SHADER, getDataDirectory() + FRAG_SHADER_SOURCE_SPLASH, "--version 150 --profile core");

			ProgramName[program::SPLASH] = glCreateProgram();
			glAttachShader(ProgramName[program::SPLASH], ShaderName[shader::VERT_SPLASH]);
			glAttachShader(ProgramName[program::SPLASH], ShaderName[shader::FRAG_SPLASH]);

			glBindFragDataLocation(ProgramName[program::SPLASH], semantic::frag::COLOR, "Color");
			glLinkProgram(ProgramName[program::SPLASH]);
		}
	
		if(Validated)
		{
			Validated = Validated && Compiler.check();
			Validated = Validated && Compiler.check_program(ProgramName[program::TEXTURE]);
			Validated = Validated && Compiler.check_program(ProgramName[program::SPLASH]);
		}

		if(Validated)
		{
			UniformTransform = glGetUniformBlockIndex(ProgramName[program::TEXTURE], "transform");
			UniformDiffuse[program::TEXTURE] = glGetUniformLocation(ProgramName[program::TEXTURE], "Diffuse");
			UniformDiffuse[program::SPLASH] = glGetUniformLocation(ProgramName[program::SPLASH], "Diffuse");

			glUseProgram(ProgramName[program::TEXTURE]);
			glUniform1i(UniformDiffuse[program::TEXTURE], 0);
			glUniformBlockBinding(ProgramName[program::TEXTURE], UniformTransform, semantic::uniform::TRANSFORM0);

			glUseProgram(ProgramName[program::SPLASH]);
			glUniform1i(UniformDiffuse[program::SPLASH], 0);
		}

		return Validated && this->checkError("initProgram");
	}

	bool initBuffer()
	{
		glGenBuffers(buffer::MAX, &BufferName[0]);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, BufferName[buffer::ELEMENT]);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, ElementSize, ElementData, GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		glBindBuffer(GL_ARRAY_BUFFER, BufferName[buffer::VERTEX]);
		glBufferData(GL_ARRAY_BUFFER, VertexSize, VertexData, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		GLint UniformBufferOffset(0);
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &UniformBufferOffset);
		GLint UniformBlockSize = glm::max(GLint(sizeof(glm::mat4)), UniformBufferOffset);

		glBindBuffer(GL_UNIFORM_BUFFER, BufferName[buffer::TRANSFORM]);
		glBufferData(GL_UNIFORM_BUFFER, UniformBlockSize, NULL, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);

		return true;
	}

	bool initTexture()
	{
		bool Validated(true);

		gli::gl GL(gli::gl::PROFILE_GL32);
		gli::texture2d Texture(gli::load_dds((getDataDirectory() + TEXTURE_DIFFUSE).c_str()));
		assert(!Texture.empty());

		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		glGenTextures(texture::MAX, &TextureName[0]);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, TextureName[texture::DIFFUSE]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(Texture.levels() - 1));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		gli::gl::format const Format = GL.translate(Texture.format(), Texture.swizzles());
		for (gli::texture2d::size_type Level = 0; Level < Texture.levels(); ++Level)
		{
			glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(Level),
				Format.Internal,
				static_cast<GLsizei>(Texture[Level].extent().x), static_cast<GLsizei>(Texture[Level].extent().y), 0,
				Format.External, Format.Type,
				Texture[Level].data());
		}

		glm::ivec2 WindowSize(this->getWindowSize() * this->FramebufferScale);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, TextureName[texture::COLORBUFFER]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, GLint(0), GL_RGBA8, GLsizei(WindowSize.x), GLsizei(WindowSize.y), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, TextureName[texture::RENDERBUFFER]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		glTexImage2D(GL_TEXTURE_2D, GLint(0), GL_DEPTH_COMPONENT24, GLsizei(WindowSize.x), GLsizei(WindowSize.y), 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

		return Validated;
	}

	bool initVertexArray()
	{
		glGenVertexArrays(program::MAX, &VertexArrayName[0]);
		glBindVertexArray(VertexArrayName[program::TEXTURE]);
			glBindBuffer(GL_ARRAY_BUFFER, BufferName[buffer::VERTEX]);
			glVertexAttribPointer(semantic::attr::POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(glf::vertex_v2fv2f), BUFFER_OFFSET(0));
			glVertexAttribPointer(semantic::attr::TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(glf::vertex_v2fv2f), BUFFER_OFFSET(sizeof(glm::vec2)));
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			glEnableVertexAttribArray(semantic::attr::POSITION);
			glEnableVertexAttribArray(semantic::attr::TEXCOORD);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, BufferName[buffer::ELEMENT]);
		glBindVertexArray(0);

		glBindVertexArray(VertexArrayName[program::SPLASH]);
		glBindVertexArray(0);

		return true;
	}

	bool initFramebuffer()
	{
		glGenFramebuffers(1, &FramebufferName);
		glBindFramebuffer(GL_FRAMEBUFFER, FramebufferName);
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, TextureName[texture::COLORBUFFER], 0);
		glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, TextureName[texture::RENDERBUFFER], 0);

		if(!this->checkFramebuffer(FramebufferName))
			return false;

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return true;
	}

	bool begin()
	{
		bool Validated = this->checkExtension("GL_ARB_buffer_storage");

		this->ReadPixelData.resize(640 * 480);

		if(Validated)
			Validated = initProgram();
		if(Validated)
			Validated = initBuffer();
		if(Validated)
			Validated = initVertexArray();
		if(Validated)
			Validated = initTexture();
		if(Validated)
			Validated = initFramebuffer();

		return Validated;
	}

	bool end()
	{
		glDeleteFramebuffers(1, &FramebufferName);
		glDeleteProgram(ProgramName[program::SPLASH]);
		glDeleteProgram(ProgramName[program::TEXTURE]);
		
		glDeleteBuffers(buffer::MAX, &BufferName[0]);
		glDeleteTextures(texture::MAX, &TextureName[0]);
		glDeleteVertexArrays(program::MAX, &VertexArrayName[0]);

		return true;
	}

	transfer* reserveTransfer()
	{
		transfer* Transfer = new transfer;
		if(ReadPixelBufferFree.empty())
		{
			glGenBuffers(1, &Transfer->Stagging);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, Transfer->Stagging);
			//glBufferData(GL_PIXEL_PACK_BUFFER, 640 * 480 * 4, nullptr, GL_STREAM_COPY);
			glBufferStorage(GL_PIXEL_PACK_BUFFER, 640 * 480 * 4, nullptr, GL_MAP_READ_BIT | GL_CLIENT_STORAGE_BIT);

			glGenBuffers(1, &Transfer->Buffer);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, Transfer->Buffer);
			//glBufferData(GL_PIXEL_PACK_BUFFER, 640 * 480 * 4, nullptr, GL_STATIC_COPY);
			glBufferStorage(GL_PIXEL_PACK_BUFFER, 640 * 480 * 4, nullptr, 0);

			Transfer->Fence = NULL;
		}
		else
		{
			Transfer = ReadPixelBufferFree.back();
			ReadPixelBufferFree.pop();
		}

		ReadPixelBufferLive.push(Transfer);

		return Transfer;
	}

	void queryTranfer()
	{
		while(!ReadPixelBufferLive.empty())
		{
			transfer* Transfer = ReadPixelBufferLive.front();

			GLint Status = 0;
			GLsizei Length = 0;
			glGetSynciv(Transfer->Fence, GL_SYNC_STATUS, 4, &Length, &Status);

			if(Status == GL_SIGNALED)
			{
				glBindBuffer(GL_PIXEL_PACK_BUFFER, Transfer->Stagging);
				void* Data = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, 640 * 480 * 4, GL_MAP_READ_BIT);
				memcpy(&ReadPixelData[0], Data, 640 * 480 * 4);
				glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

				ReadPixelBufferFree.push(Transfer);
				ReadPixelBufferLive.pop();
			}
			else
			{
				break;
			}
		}
	}

	bool render()
	{
		glm::vec2 WindowSize(this->getWindowSize());

		transfer* TransferFBO = reserveTransfer();
		transfer* TransferFB = reserveTransfer();

		{
			glBindBuffer(GL_UNIFORM_BUFFER, BufferName[buffer::TRANSFORM]);
			glm::mat4* Pointer = (glm::mat4*)glMapBufferRange(
				GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4),
				GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);

			//glm::mat4 Projection = glm::perspectiveFov(glm::pi<float>() * 0.25f, 640.f, 480.f, 0.1f, 100.0f);
			glm::mat4 Projection = glm::perspective(glm::pi<float>() * 0.25f, WindowSize.x / WindowSize.y, 0.1f, 100.0f);
			glm::mat4 Model = glm::mat4(1.0f);
		
			*Pointer = Projection * this->view() * Model;

			// Make sure the uniform buffer is uploaded
			glUnmapBuffer(GL_UNIFORM_BUFFER);
		}

		{
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LESS);

			glViewport(0, 0, static_cast<GLsizei>(WindowSize.x) * this->FramebufferScale, static_cast<GLsizei>(WindowSize.y) * this->FramebufferScale);

			glBindFramebuffer(GL_FRAMEBUFFER, FramebufferName);
			//glEnable(GL_FRAMEBUFFER_SRGB);
			float Depth(1.0f);
			glClearBufferfv(GL_DEPTH , 0, &Depth);
			glClearBufferfv(GL_COLOR, 0, &glm::vec4(1.0f, 0.5f, 0.0f, 1.0f)[0]);

			glUseProgram(ProgramName[program::TEXTURE]);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, TextureName[texture::DIFFUSE]);
			glBindVertexArray(VertexArrayName[program::TEXTURE]);
			glBindBufferBase(GL_UNIFORM_BUFFER, semantic::uniform::TRANSFORM0, BufferName[buffer::TRANSFORM]);

			glDrawElementsInstancedBaseVertex(GL_TRIANGLES, ElementCount, GL_UNSIGNED_SHORT, 0, 1, 0);

			glBindBuffer(GL_PIXEL_PACK_BUFFER, TransferFBO->Buffer);
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, 0);
			glBindBuffer(GL_COPY_READ_BUFFER, TransferFBO->Buffer);
			glBindBuffer(GL_COPY_WRITE_BUFFER, TransferFBO->Stagging);
			glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, 640 * 480 * 4);
			TransferFBO->Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		}

		{
			glDisable(GL_DEPTH_TEST);

			glViewport(0, 0, static_cast<GLsizei>(WindowSize.x), static_cast<GLsizei>(WindowSize.y));

			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glUseProgram(ProgramName[program::SPLASH]);

			glActiveTexture(GL_TEXTURE0);
			glBindVertexArray(VertexArrayName[program::SPLASH]);
			glBindTexture(GL_TEXTURE_2D, TextureName[texture::COLORBUFFER]);

			glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);

			glBindBuffer(GL_PIXEL_PACK_BUFFER, TransferFB->Buffer);
			glReadBuffer(GL_BACK);
			glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, 0);
			glBindBuffer(GL_COPY_READ_BUFFER, TransferFB->Buffer);
			glBindBuffer(GL_COPY_WRITE_BUFFER, TransferFB->Stagging);
			glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, 640 * 480 * 4);
			TransferFB->Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		}

		this->queryTranfer();

		printf("Live %d, Free %d\r", static_cast<int>(ReadPixelBufferLive.size()), static_cast<int>(ReadPixelBufferFree.size()));

		return true;
	}
};

int main(int argc, char* argv[])
{
	int Error(0);

	gl_440_fbo_readpixels_async Test(argc, argv);
	Error += Test();

	return Error;
}


/*=====================================================================
LLMThread.cpp
-------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "LLMThread.h"


#include "ServerWorldState.h" // Just for ServerCredentials
#include <ConPrint.h>
#include <Exception.h>
#include <JSONParser.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Parser.h>
#include <RuntimeCheck.h>
#include <Timer.h>
#include <networking/HTTPClient.h>
#include <webserver/Escaping.h>


LLMThread::LLMThread(const std::string& AI_model_id_)
:	cur_ai_model_is_anthropic(false),
	AI_model_id(AI_model_id_)
{
}


LLMThread::~LLMThread()
{
}


void LLMThread::doRun()
{
	try
	{
		PlatformUtils::setCurrentThreadNameIfTestsEnabled("LLMThread");

		std::vector<AIModel> models;
		{
			AIModel model;
			model.id_string = "openai/gpt-4o";
			model.api_id_string = "gpt-4o";
			model.name = "GPT-4o";
			model.description = "GPT-4o is OpenAI's versatile, high-intelligence flagship model.";
			model.api_domain = "api.openai.com";
			model.api_key_credential_name = "openai_api_key";
			models.push_back(model);
		}
		{
			AIModel model;
			model.id_string = "openai/gpt-4o-mini";
			model.api_id_string = "gpt-4o-mini";
			model.name = "GPT-4o mini";
			model.description = "GPT-4o mini is OpenAI's fast, affordable small model for focused tasks.";
			model.api_domain = "api.openai.com";
			model.api_key_credential_name = "openai_api_key";
			models.push_back(model);
		}
		{
			AIModel model;
			model.id_string = "xai/grok-4";
			model.api_id_string = "grok-4";
			model.name = "Grok 4";
			model.description = "X.AI's latest and greatest flagship model, offering unparalleled performance in natural language, math and reasoning - the perfect jack of all trades.";
			model.api_domain = "api.x.ai";
			model.api_key_credential_name = "xai_api_key";
			models.push_back(model);
		}
	
		{
			AIModel model;
			model.id_string = "xai/grok-4-1-fast-non-reasoning";
			model.api_id_string = "grok-4-1-fast-non-reasoning";
			model.name = "Grok 4.1 Fast (Non-Reasoning)";
			model.description = "From xAI. A frontier multimodal model optimized specifically for high-performance agentic tool calling.";
			model.api_domain = "api.x.ai";
			model.api_key_credential_name = "xai_api_key";
			models.push_back(model);
		}

		AIModel cur_ai_model;
		for(size_t i=0; i<models.size(); ++i)
			if(models[i].id_string == this->AI_model_id)
				cur_ai_model = models[i];

		if(cur_ai_model.id_string.empty())
			throw glare::Exception("Failed to find AI model with id '" + this->AI_model_id + "'");


		this->cur_ai_model_is_anthropic = cur_ai_model.api_domain == "api.anthropic.com";

		Reference<HTTPClient> http_client; // For connecting to AI/LLM server

		js::Vector<ThreadMessageRef, 16> temp_messages;

		while(!should_quit)
		{
			try
			{
				this->getMessageQueue().dequeueAllQueuedItemsBlocking(temp_messages);

				// Handle messages
				for(size_t i=0; i<temp_messages.size(); ++i)
				{
					ThreadMessageRef msg_ = temp_messages[i];

					if(dynamic_cast<KillThreadMessage*>(msg_.ptr()))
					{
						should_quit = true;
						break;
					}
					else if(dynamic_cast<SendAIChatPostContent*>(msg_.ptr()))
					{
						// Append a user chat message to the chat history, and send to LLM server.
						SendAIChatPostContent* msg = static_cast<SendAIChatPostContent*>(msg_.ptr());

						conPrint("LLMThread: received SendAIChatPostContent with message '" + msg->message + "'.");

						// Append message to back of chat history
						{
							LLMChatMessage new_user_msg;
							new_user_msg.role = LLMChatMessage::Role_User;
							new_user_msg.content = msg->message;
							this->chat_messages.push_back(new_user_msg);
						}

						trimChatMessageHistory();

						sendChatRequestToLLMServer(cur_ai_model, http_client);
					}
					else if(dynamic_cast<SendAIChatToolCallResult*>(msg_.ptr()))
					{
						// Append a tool call function result to the chat history, and send to LLM server.
						SendAIChatToolCallResult* msg = static_cast<SendAIChatToolCallResult*>(msg_.ptr());

						conPrint("LLMThread: received SendAIChatToolCallResult");

						// Append message to back of chat history
						{
							LLMChatMessage new_user_msg;
							new_user_msg.role = LLMChatMessage::Role::Role_Tool;
							new_user_msg.tool_call_id = msg->tool_call_id;
							new_user_msg.tool_call_name = msg->tool_call_name;
							new_user_msg.content = msg->content;
							this->chat_messages.push_back(new_user_msg);
						}

						trimChatMessageHistory();

						if(msg->should_send_to_server_immediately)
							sendChatRequestToLLMServer(cur_ai_model, http_client);
					}
				} // end for each message
			}
			catch(glare::Exception& e)
			{
				conPrint("LLMThread: glare::Exception: " + e.what());
				PlatformUtils::Sleep(1000);
			}
			catch(std::bad_alloc&)
			{
				conPrint("LLMThread: Caught std::bad_alloc.");
				PlatformUtils::Sleep(1000);
			}
		} // end while(!should_quit)
	}
	catch(glare::Exception& e)
	{
		conPrint("LLMThread: glare::Exception: " + e.what());
	}
}


void LLMThread::sendChatRequestToLLMServer(const AIModel& cur_ai_model, Reference<HTTPClient>& http_client)
{
	// Construct HTTP content payload to send to LLM server.
	std::string post_content = "{\"model\": \"";
	post_content += cur_ai_model.api_id_string;
	post_content += "\", ";
								
	if(cur_ai_model_is_anthropic)
	{
		post_content += "\"max_tokens\": 1024,";
		post_content += "\"system\": \"" +this->base_prompt_json_escaped + "\","; // For Claude, the system prompt is separate and not part of the chat messages
	}

	//------------ Write messages array -----------
	post_content += "\"messages\": [";

	if(!cur_ai_model_is_anthropic) // For Claude, the system prompt is separate and not part of the chat messages, so don't include it here.
		post_content += "{\"role\": \"system\", \"content\": \"" + this->base_prompt_json_escaped + "\"},";

	for(size_t z=0; z<this->chat_messages.size(); ++z)
	{
		const LLMChatMessage& message = this->chat_messages[z];

		post_content += "{\"role\":\"";
		if(message.role == LLMChatMessage::Role_User)
			post_content += "user";
		else if(message.role == LLMChatMessage::Role_Tool)
			post_content += "tool";
		else
			post_content += "assistant";

		post_content += "\",";

		if(message.role == LLMChatMessage::Role_Tool)
		{
			post_content += "\"tool_call_id\":\"" + web::Escaping::JSONEscape(message.tool_call_id) + "\",";
			post_content += "\"name\":\"" + web::Escaping::JSONEscape(message.tool_call_name) + "\",";
		}

		post_content += "\"content\":\"";
		post_content += web::Escaping::JSONEscape(message.content);
		post_content += "\"";

		if(!message.tool_calls.empty())
		{
			post_content += ",\"tool_calls\":[";
			for(size_t t=0; t<message.tool_calls.size(); ++t)
			{
				const ToolFunctionCall* call = message.tool_calls[t].ptr();
				post_content += "{\"id\":\"" + web::Escaping::JSONEscape(call->call_id) + "\",";
				post_content += "\"type\": \"function\",";
				post_content += "\"function\": {";
				post_content += "	\"name\":\"" + web::Escaping::JSONEscape(call->function_name) + "\",";
				post_content += "	\"arguments\": \"{}\""; // JSON string of args.  TEMP: assuming zero args.
				post_content += "}}";

				if((t + 1) < message.tool_calls.size())
					post_content += ",";
			}
			post_content += "]";
		}

		post_content += "}";

		if(z + 1 < this->chat_messages.size())
			post_content += ",";
	}
	post_content += "	],";
	//------------ End write messages array -----------

	post_content += this->tools_json;

	if(cur_ai_model.api_domain == "api.together.xyz")
		post_content += "	\"stream_tokens\": true ";
	else
		post_content += "	\"stream\": true ";
	post_content += "}";


	// TEMP: check post_content is valid JSON
#ifndef NDEBUG
	try
	{
		JSONParser parser;
		parser.parseBuffer(post_content.data(), post_content.size());
	}
	catch(glare::Exception& e)
	{
		conPrint("invalid JSON: " + e.what());
	}
#endif
	//-------------------------------------------------------------------------------------------------

	// conPrint("post_content: " + post_content);

	if(!http_client)
	{
		try
		{
			http_client = createHTTPClient(cur_ai_model);
		}
		catch(glare::Exception& e)
		{
			conPrint("Error: Failed to create HTTP client: " + e.what());
		}
	}

	if(http_client)
	{
		const int max_num_retries = 3;
		for(int z=0; z<max_num_retries; ++z)
		{
			try
			{
				// Clear buffer that we will stream data into and search locations
				next_nonempty_line_start = 0;
				newline_search_pos = 0;
				http_response_data.clear();
				current_assistant_response = LLMChatMessage(); // Clear current_assistant_response
				current_assistant_response.role = LLMChatMessage::Role_Assistant;

				std::string path;
				if(cur_ai_model_is_anthropic)
					path = "/v1/messages";
				else if(cur_ai_model.api_domain == "api.fireworks.ai")
					path = "/inference/v1/chat/completions";
				else
					path = "/v1/chat/completions"; // OpenAI path

				Timer timer;
				HTTPClient::ResponseInfo response_info = http_client->sendPost(
					"https://" + cur_ai_model.api_domain + path,
					post_content, 
					"application/json", // content type
					/*handler=*/*this
				);

				conPrint("HTTP response took " + timer.elapsedString());
				conPrint("HTTP Response code: " + toString(response_info.response_code) + ", msg: '" + response_info.response_message + "'");
				if(response_info.response_code >= 400)
				{
					conPrint("http_response_data: '" + std::string(http_response_data.data(), http_response_data.size()) + "'");

					// TODO; report some kind of failure message? 
				}
				
				break;
			}
			catch(glare::Exception& e)
			{
				conPrint("Connection to AI server exception: " + e.what());
				if(z + 1 < max_num_retries)
					conPrint("Retrying...");
				if(z > 0)
					PlatformUtils::Sleep(1000);

				http_client->resetConnection(); // Set socket to null so the next sendPost() reconnects.
			}
		}
	}
}


void LLMThread::kill()
{
	should_quit = true;
}


// Remove old messages if there are too many.
// TODO: also remove messages to keep total token size below some threshold.
void LLMThread::trimChatMessageHistory()
{
	const size_t MAX_NUM_MESSAGES = 50;
	while(chat_messages.size() > MAX_NUM_MESSAGES)
		chat_messages.pop_front();
}


Reference<HTTPClient> LLMThread::createHTTPClient(const AIModel& cur_ai_model)
{
	const auto res = credentials->creds.find(cur_ai_model.api_key_credential_name);
	if(res == credentials->creds.end())
		throw glare::Exception("ERROR: WorkerThread::createHTTPClient(): couldn't find credentials '" + cur_ai_model.api_key_credential_name + "'");
	const std::string api_key = res->second;

	conPrint("Making new HTTP connection to '" + cur_ai_model.api_domain + "'...");

	Reference<HTTPClient> http_client = new HTTPClient();
	http_client->max_socket_buffer_size = 1024 * 1024; // Can hit the limit with the default 2^16 size, so increase it.
	http_client->enable_TCP_nodelay = true; // Since we are doing realtime chat, we want to send off messages without delay.
	
	if(cur_ai_model.api_domain == "api.anthropic.com")
	{
		http_client->additional_headers.push_back("x-api-key: " + api_key);
		http_client->additional_headers.push_back("anthropic-version: 2023-06-01"); // See https://docs.anthropic.com/en/api/versioning
	}
	else
	{
		// Else OpenAI or OpenAI-compatible:
		http_client->additional_headers.push_back("Authorization: Bearer " + api_key);
	}

	http_client->connectAndEnableKeepAlive("https", cur_ai_model.api_domain, /*port=*/-1);

	return http_client;
}


// Called when we receive data back from the HTTP client
// This will be streaming data using server-sent-events (SSE).
void LLMThread::handleData(ArrayRef<uint8> chunk, const HTTPClient::ResponseInfo& response_info)
{
	if(chunk.size() > 0)
	{
		// Append to http_response_data
		const size_t cur_len = http_response_data.size();
		http_response_data.resize(cur_len + chunk.size());
		std::memcpy(&http_response_data[cur_len], chunk.data(), chunk.size());

		const size_t MAX_RESPONSE_SIZE = 1'000'000;
		if(http_response_data.size() > MAX_RESPONSE_SIZE)
			throw glare::Exception("LLMThread::handleData(): Response too large");

		if(response_info.response_code >= 200 && response_info.response_code < 300)
		{
			// Search for double-newline
			while(newline_search_pos + 1 < http_response_data.size())
			{
				if(
					(http_response_data[newline_search_pos  ] == '\n' || http_response_data[newline_search_pos  ] == '\r') &&
					(http_response_data[newline_search_pos+1] == '\n' || http_response_data[newline_search_pos+1] == '\r')
					)
				{
					// Found double newline.
					runtimeCheck(newline_search_pos >= next_nonempty_line_start);
					const size_t message_size = newline_search_pos - next_nonempty_line_start;
					runtimeCheck(next_nonempty_line_start + message_size <= http_response_data.size());
					Parser parser(http_response_data.data() + next_nonempty_line_start, /*size=*/message_size);

					// Note: should we handle server-sent-event comments? (begins with ':', see https://developer.mozilla.org/en-US/docs/Web/API/Server-sent%5Fevents/Using%5Fserver-sent%5Fevents)

					// Handle event name if present (anthropic)
					if(parser.parseCString("event:"))
					{
						parser.parseWhiteSpace();
						string_view event_name;
						parser.parseIdentifier(event_name);
						parser.parseWhiteSpace(); // Parse newline
					}
					if(parser.parseCString("data:"))
					{
						parser.parseWhiteSpace();

						runtimeCheck(parser.currentPos() <= parser.getTextSize());
						const string_view value(parser.getText() + parser.currentPos(), parser.getTextSize() - parser.currentPos());

						//if(cur_ai_model_is_anthropic)
						//{
						//	// We are expecting a JSON object
						//	JSONParser json_parser;
						//	json_parser.parseBuffer(value.data(), value.size());
						//
						//	const std::string& type = json_parser.nodes[0].getChildStringValue(json_parser, "type");
						//	if(type == "content_block_start")
						//	{
						//		const JSONNode& content_block_node = json_parser.nodes[0].getChildNode(json_parser, "content_block");
						//		const std::string& content_text = content_block_node.getChildStringValue(json_parser, "text");
						//
						//		// Send AIChatResponseDataMessage message to client
						//		AIChatResponseDataMessage* msg = new AIChatResponseDataMessage();
						//		msg->message = content_text;
						//		out_msg_queue->enqueue(msg);
						//	}
						//	else if(type == "content_block_delta")
						//	{
						//		const JSONNode& delta_node = json_parser.nodes[0].getChildNode(json_parser, "delta");
						//		const std::string& delta_text = delta_node.getChildStringValue(json_parser, "text");
						//
						//		// Send AIChatResponseDataMessage message to client
						//		AIChatResponseDataMessage* msg = new AIChatResponseDataMessage();
						//		msg->message = delta_text;
						//		out_msg_queue->enqueue(msg);
						//	}
						//	else if(type == "content_block_stop")
						//	{
						//		// Add to chat history
						//		//this->chat_messages.push_back(ChatMessage({/*role=*/ChatMessage::Role_Assistant, /*content=*/total_llm_response}));
						//
						//		out_msg_queue->enqueue(new AIChatResponseDoneMessage());
						//	}
						//}
						//else
						{
							if(parser.parseCString("[DONE]"))
							{
								conPrint("=======Received [DONE]======");

								// Add total response to chat history
								this->chat_messages.push_back(current_assistant_response);
								
								// Clear current_assistant_response
								current_assistant_response = LLMChatMessage();
								current_assistant_response.role = LLMChatMessage::Role_Assistant;
								
								AIChatResponseDoneMessage* done_msg = new AIChatResponseDoneMessage();
								done_msg->chatbot = chatbot;
								out_msg_queue->enqueue(done_msg);
							}
							else
							{
								JSONParser json_parser;
								json_parser.parseBuffer((const char*)value.data(), value.size());

								const JSONNode& root = json_parser.nodes[0];
								checkNodeType(root, JSONNode::Type_Object);

								const JSONNode& choices_node = root.getChildArray(json_parser, "choices");
								if(choices_node.child_indices.empty())
									throw glare::Exception("Choices array was empty.");

								//for(size_t i=0; i<choices_node.child_indices.size(); ++i)
								{
									const JSONNode& choice_node = json_parser.nodes[choices_node.child_indices[0]]; // Use first (zeroth) choice.

									const JSONNode& delta_node = choice_node.getChildObject(json_parser, "delta");

									if(delta_node.hasChild("content"))
									{
										//const std::string role = delta_node.getChildStringValue(json_parser, "role");
										const std::string& content = delta_node.getChildStringValue(json_parser, "content");

										conPrint("------Received content:------\n" + content);

										this->current_assistant_response.content += content;

										//conPrint("------total_llm_response:------\n" + total_llm_response);

										// Send AIChatResponseDataMessage message to client
										AIChatResponseDataMessage* msg = new AIChatResponseDataMessage();
										msg->message = content;
										msg->chatbot = chatbot;
										out_msg_queue->enqueue(msg);
									}

									if(delta_node.hasChild("tool_calls"))
									{
										const JSONNode& tool_calls_node = delta_node.getChildArray(json_parser, "tool_calls");

										Reference<AIToolFunctionCallMessage> call_msg = new AIToolFunctionCallMessage();
										call_msg->chatbot = chatbot;

										for(size_t i=0; i<tool_calls_node.child_indices.size(); ++i)
										{
											const JSONNode& call_node = json_parser.nodes[tool_calls_node.child_indices[i]];

											Reference<ToolFunctionCall> call = new ToolFunctionCall();

											call->call_id = call_node.getChildStringValue(json_parser, "id");

											const JSONNode& function_node = call_node.getChildObject(json_parser, "function");

											call->function_name = function_node.getChildStringValue(json_parser, "name");

											call_msg->calls.push_back(call);
										}

										out_msg_queue->enqueue(call_msg);

										this->current_assistant_response.tool_calls = call_msg->calls;
									}
								}
							}
						}
					}

					// Advance past the double-newline chars we just found
					next_nonempty_line_start = newline_search_pos + 2;
					newline_search_pos       = newline_search_pos + 2;
				}
				else
					newline_search_pos++;
			}
		}
	}
}

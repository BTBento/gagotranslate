from flask import Flask, request, jsonify, send_file, make_response
import boto3
import io
import time
import requests

app = Flask(__name__)

translate_client = boto3.client('translate', region_name='us-west-1')
polly_client = boto3.client('polly', region_name='us-west-1')
transcribe_client = boto3.client('transcribe', region_name='us-west-1')
s3_client = boto3.client('s3', region_name='us-west-1')

BUCKET_NAME = "gago-translate-audio-147"

LANGUAGES = {
    'English': 'en-US',
    'Spanish': 'es-ES',
    'French': 'fr-FR',
    'Chinese': 'zh-CN',
    'Japanese': 'ja-JP',
    'German': 'de-DE'
}

VOICES = {
    'en-US': 'Joanna',
    'es-ES': 'Penelope',
    'fr-FR': 'Lea',
    'zh-CN': 'Zhiyu',
    'ja-JP': 'Mizuki',
    'de-DE': 'Vicki'
}

@app.route("/")
def index():
    return "Gago Translate"


@app.route("/upload", methods=['GET', 'POST'])
def upload_audio():
    if request.method == 'POST':
        try:
            print("Received request")
            print(f"Content-Type: {request.headers.get('Content-Type')}")
            print(f"Content-length: {request.headers.get('Content-Length')}")
            print("Files: ", request.files)
            print("Get_data: ", request.get_data())
            audio_data = request.get_data()
            print(f"Received audio data length: {len(audio_data)}")
            if len(audio_data) == 0:
                return jsonify({"err": "No audio data received"}), 400
            """
            if 'file' in request.files:
                audio_data = request.files['file'].read()
            else:
                audio_data = request.get_data()
            """
            print(f"Received audio data length: {len(audio_data)}")
            source_lang = request.args.get("source", "en")
            target_lang = request.args.get("target", "es")
            print(f"Languages: {source_lang} -> {target_lang}")

            # Map short codes to full codes
            lang_map = {
                'en': 'en-US',
                'es': 'es-ES',
                'fr': 'fr-FR',
                'zh': 'zh-CN',
                'ja': 'ja-JP',
                'de': 'de-DE'
            }

            source_lang = lang_map.get(source_lang, source_lang)
            target_lang = lang_map.get(target_lang, target_lang)
            print(f"Lang: {source_lang} -> {target_lang}")

            timestamp = str(int(time.time()))
            audio_file_name = f"audio_{timestamp}.wav"
            s3_path = f"https://s3.us-west-1.amazonaws.com/{BUCKET_NAME}/{audio_file_name}"

            try:
                print(f"Uploading to S3: {audio_file_name}")
                s3_client.put_object(
                    Bucket=BUCKET_NAME,
                    Key=audio_file_name,
                    Body=audio_data
                )
                print("Upload succeed")
            except Exception as e:
                print(f"S3 upload err: {str(e)}")
                return f"S3 upload failed:{str(e)}", 500

            job_name = f"transcribe_{timestamp}"
            transcribe_client.start_transcription_job(
                TranscriptionJobName=job_name,
                Media={'MediaFileUri': s3_path},
                MediaFormat='wav',
                LanguageCode=source_lang
            )

            print("Waiting for transcription...")
            while True:
                status = transcribe_client.get_transcription_job(TranscriptionJobName=job_name)
                if status['TranscriptionJob']['TranscriptionJobStatus'] in ['COMPLETED', 'FAILED']:
                    break
                time.sleep(1)
                print(".", end="", flush=True)

            if status['TranscriptionJob']['TranscriptionJobStatus'] == 'COMPLETED':
                try:
                    transcript_uri = status['TranscriptionJob']['Transcript']['TranscriptFileUri']
                    print(f"\nTranscript URI: {transcript_uri}")

                    transcript_response = requests.get(transcript_uri)
                    transcript_data = transcript_response.json()
                    transcribed_text = transcript_data['results']['transcripts'][0]['transcript']
                    print(f"Transcribed text: {transcribed_text}")

                    if transcribed_text:
                        translation = translate_client.translate_text(
                            Text=transcribed_text,
                            SourceLanguageCode=source_lang,
                            TargetLanguageCode=target_lang
                        )
                        translated_text = translation['TranslatedText']
                        print(f"Translated text: {translated_text}")

                        voice = VOICES.get(target_lang, 'Joanna')
                        polly_response = polly_client.synthesize_speech(
                            Text=translated_text,
                            OutputFormat='mp3',
                            VoiceId=voice
                        )

                        s3_client.delete_object(
                            Bucket=BUCKET_NAME,
                            Key=audio_file_name
                        )

                        audio_stream = io.BytesIO(polly_response['AudioStream'].read())
                        audio_stream.seek(0)

                        response = send_file(
                            audio_stream,
                            mimetype='audio/mpeg',
                            as_attachment=False
                        )

                        response.headers['X-Original'] = transcribed_text
                        response.headers['X-Translation'] = translated_text
                        return response
                    else:
                        return "No transcribed text available", 400

                except Exception as e:
                    print(f"Error processing transcription: {str(e)}")
                    return f"Error processing transcription: {str(e)}", 500
            else:
                print("Transcribe failed")
                return "Transcription failed", 500

        except Exception as e:
            print(f"Error in upload_audio: {str(e)}")
            import traceback
            traceback.print_exc()
            return str(e), 500
    elif request.method == 'GET':
        return jsonify({
            "available_languages": LANGUAGES,
            "usage": {
                "endpoint": "/upload",
                "method": "POST",
                "parameters": {
                    "source": "Language code for input (e.g., en-US)",
                    "target": "Language code for output (e.g., es-ES)"
                },
                "content-type": "audio/wav",
                "example": "/upload?source=en-US&target=es-ES"
            }
        })
    else:
        return "WHAT THE?!"

@app.route("/languages")
def get_languages():
    return jsonify(LANGUAGES)

if __name__ == '__main__':
    app.run(host='0.0.0.0')

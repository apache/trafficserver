/*
 * ImageTransfrom.h
 *
 *  Created on: Aug 6, 2015
 *      Author: sdavu
 */

#ifndef IMAGETRANSFROM_H_
#define IMAGETRANSFROM_H_

#include <string>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransactionPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/Transaction.h>

struct ImageValue: public atscppapi::Transaction::ContextValue{
	bool do_transform_;
	ImageValue(bool transform) : do_transform_(transform) { }
};


class ImageTransform : public atscppapi::TransformationPlugin {
public:
	ImageTransform(atscppapi::Transaction &transaction, atscppapi::TransformationPlugin::Type xformType);

  void handleReadResponseHeaders(atscppapi::Transaction &transaction);
  void consume(const std::string &data);
  void handleInputComplete();
  virtual ~ImageTransform();

  static std::string FIELD_USER_AGENT;
  static std::string FIELD_TRANSFORM_IMAGE;
  static std::string CONTEXT_IMG_TRANSFORM;
  static std::string USER_AGENT_CROME;
  static std::string FIELD_CONTENT_TYPE;

private:
  std::stringstream img_;
  WebpTransform webp_transform_;
};

class GlobalHookPlugin : public atscppapi::GlobalPlugin {
public:
  GlobalHookPlugin();
  virtual void handleReadRequestHeaders(atscppapi::Transaction &transaction);
  virtual void handleReadResponseHeaders(atscppapi::Transaction &transaction);
};



#endif /* IMAGETRANSFROM_H_ */

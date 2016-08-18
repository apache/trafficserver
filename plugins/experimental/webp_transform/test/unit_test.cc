#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "MockAtscppapi.h"
#include "ImageTransform.h"

using ::testing::ReturnRef;
using ::testing::Return;
using ::testing::_;

class GlobalHookPluginTest : public ::testing::Test
{
protected:
  void *arg1;
  Headers hdrs;
  Response response;
  string ctype, uagent;
  Request srequest;

  virtual void
  SetUp()
  {
    arg1   = NULL;
    ctype  = "Content-Type";
    uagent = "User-Agent";
  }
};

TEST_F(GlobalHookPluginTest, NoContentTypeandUserAgent)
{
  Transaction transaction(arg1);
  EXPECT_CALL(transaction, getServerResponse()).WillRepeatedly(ReturnRef(response));
  EXPECT_CALL(response, getHeaders()).WillOnce(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, values(ctype)).WillOnce(Return(""));
  EXPECT_CALL(transaction, getServerRequest()).WillOnce(ReturnRef(srequest));
  EXPECT_CALL(srequest, getHeaders()).WillOnce(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, values(uagent)).WillOnce(Return(""));
  EXPECT_CALL(transaction, resume()).Times(1);

  GlobalHookPlugin globalHookPlugin;
  globalHookPlugin.handleReadResponseHeaders(transaction);
}

TEST_F(GlobalHookPluginTest, NoContentTyep)
{
  Transaction transaction(arg1);
  EXPECT_CALL(transaction, getServerResponse()).WillRepeatedly(ReturnRef(response));
  EXPECT_CALL(response, getHeaders()).WillOnce(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, values(ctype)).WillOnce(Return(""));
  EXPECT_CALL(transaction, getServerRequest()).WillOnce(ReturnRef(srequest));
  EXPECT_CALL(srequest, getHeaders()).WillOnce(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, values(uagent)).WillOnce(Return("Chrome"));
  EXPECT_CALL(transaction, resume()).Times(1);

  GlobalHookPlugin globalHookPlugin;
  globalHookPlugin.handleReadResponseHeaders(transaction);
}

TEST_F(GlobalHookPluginTest, NoUserAgent)
{
  Transaction transaction(arg1);
  EXPECT_CALL(transaction, getServerResponse()).WillRepeatedly(ReturnRef(response));
  EXPECT_CALL(response, getHeaders()).WillOnce(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, values(ctype)).WillOnce(Return("image/jpeg"));
  EXPECT_CALL(transaction, getServerRequest()).WillOnce(ReturnRef(srequest));
  EXPECT_CALL(srequest, getHeaders()).WillOnce(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, values(uagent)).WillOnce(Return(""));
  EXPECT_CALL(transaction, resume()).Times(1);

  GlobalHookPlugin globalHookPlugin;
  globalHookPlugin.handleReadResponseHeaders(transaction);
}

TEST_F(GlobalHookPluginTest, PngandChrome)
{
  Transaction transaction(arg1);
  EXPECT_CALL(transaction, getServerResponse()).WillRepeatedly(ReturnRef(response));
  EXPECT_CALL(response, getHeaders()).WillOnce(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, values(ctype)).WillOnce(Return("image/png"));
  EXPECT_CALL(transaction, getServerRequest()).WillOnce(ReturnRef(srequest));
  EXPECT_CALL(srequest, getHeaders()).WillOnce(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, values(uagent)).WillOnce(Return("Chrome"));
  EXPECT_CALL(transaction, addPlugin(_)).Times(1);
  EXPECT_CALL(transaction, resume()).Times(1);

  GlobalHookPlugin globalHookPlugin;
  globalHookPlugin.handleReadResponseHeaders(transaction);
}

TEST_F(GlobalHookPluginTest, JpegandChrome)
{
  Transaction transaction(arg1);
  EXPECT_CALL(transaction, getServerResponse()).WillRepeatedly(ReturnRef(response));
  EXPECT_CALL(response, getHeaders()).WillOnce(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, values(ctype)).WillOnce(Return("image/jpeg"));
  EXPECT_CALL(transaction, getServerRequest()).WillOnce(ReturnRef(srequest));
  EXPECT_CALL(srequest, getHeaders()).WillOnce(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, values(uagent)).WillOnce(Return("Chrome"));
  EXPECT_CALL(transaction, addPlugin(_)).Times(1);
  EXPECT_CALL(transaction, resume()).Times(1);

  GlobalHookPlugin globalHookPlugin;
  globalHookPlugin.handleReadResponseHeaders(transaction);
}

class ImageTransformTest : public ::testing::Test
{
protected:
  void *arg1;
  const void *ret;
  Response response;
  Headers hdrs;
  string ctype, vary;
  HeaderField hfield;
  Request request;
  Url url;

  virtual void
  SetUp()
  {
    arg1 = NULL;
    ret = NULL;
    ctype = "Content-Type";
    vary = "Vary";
  }
};

TEST_F(ImageTransformTest, ImageInputComplete)
{
  Transaction transaction(arg1);
  ImageTransform imageTransform(transaction);
  Blob &input_blob  = imageTransform.getInputBlob();
  Blob &output_blob = imageTransform.getOutputBlob();
  Image &image      = imageTransform.getImageObject();

  EXPECT_CALL(input_blob, update(_, _)).Times(1);
  EXPECT_CALL(image, read(_)).Times(1);
  EXPECT_CALL(image, magick("WEBP")).Times(1);
  EXPECT_CALL(image, write(&output_blob)).Times(1);
  EXPECT_CALL(output_blob, data()).WillOnce(Return(ret));
  EXPECT_CALL(output_blob, length()).WillOnce(Return(0));

  imageTransform.handleInputComplete();
}

TEST_F(ImageTransformTest, ImageSetResponseHeaders)
{
  Transaction transaction(arg1);
  EXPECT_CALL(transaction, getServerResponse()).Times(2).WillRepeatedly(ReturnRef(response));
  EXPECT_CALL(response, getHeaders()).Times(2).WillRepeatedly(ReturnRef(hdrs));
  EXPECT_CALL(hdrs, assign_at(ctype)).WillOnce(Return(hfield));
  EXPECT_CALL(hdrs, assign_at(vary)).WillOnce(Return(hfield));
  EXPECT_CALL(transaction, getServerRequest()).WillRepeatedly(ReturnRef(request));
  EXPECT_CALL(request, getUrl()).WillOnce(ReturnRef(url));
  EXPECT_CALL(url, getUrlString()).WillOnce(Return(""));
  EXPECT_CALL(transaction, resume()).Times(1);

  ImageTransform imageTransform(transaction);
  imageTransform.handleReadResponseHeaders(transaction);
}

int
main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

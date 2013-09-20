// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/renderer/aw_content_renderer_client.h"

#include "android_webview/common/aw_resource.h"
#include "android_webview/common/render_view_messages.h"
#include "android_webview/common/url_constants.h"
#include "android_webview/renderer/aw_render_view_ext.h"
// START: Printing fork b/10190508
#include "android_webview/renderer/print_web_view_helper.h"
// END: Printing fork b/10190508
#include "base/message_loop/message_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/content/renderer/autofill_agent.h"
#include "components/autofill/content/renderer/password_autofill_agent.h"
#include "components/visitedlink/renderer/visitedlink_slave.h"
#include "content/public/renderer/document_state.h"
#include "content/public/renderer/navigation_state.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/render_view.h"
#include "net/base/net_errors.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/web/WebNavigationType.h"
#include "third_party/WebKit/public/web/WebSecurityPolicy.h"
#include "url/gurl.h"

namespace android_webview {

AwContentRendererClient::AwContentRendererClient() {
}

AwContentRendererClient::~AwContentRendererClient() {
}

void AwContentRendererClient::RenderThreadStarted() {
  WebKit::WebString content_scheme(
      ASCIIToUTF16(android_webview::kContentScheme));
  WebKit::WebSecurityPolicy::registerURLSchemeAsLocal(content_scheme);

  content::RenderThread* thread = content::RenderThread::Get();

  aw_render_process_observer_.reset(new AwRenderProcessObserver);
  thread->AddObserver(aw_render_process_observer_.get());

  visited_link_slave_.reset(new visitedlink::VisitedLinkSlave);
  thread->AddObserver(visited_link_slave_.get());
}

bool AwContentRendererClient::HandleNavigation(
    content::RenderView* view,
    content::DocumentState* document_state,
    WebKit::WebFrame* frame,
    const WebKit::WebURLRequest& request,
    WebKit::WebNavigationType type,
    WebKit::WebNavigationPolicy default_policy,
    bool is_redirect) {

  // Only GETs can be overridden.
  if (!request.httpMethod().equals("GET"))
    return false;

  // Any navigation from loadUrl, and goBack/Forward are considered application-
  // initiated and hence will not yield a shouldOverrideUrlLoading() callback.
  // Webview classic does not consider reload application-initiated so we
  // continue the same behavior.
  bool application_initiated =
      !document_state->navigation_state()->is_content_initiated()
      || type == WebKit::WebNavigationTypeBackForward;

  // Don't offer application-initiated navigations unless it's a redirect.
  if (application_initiated && !is_redirect)
    return false;

  // We are only interested in top-level navigation.
  if (frame->parent())
    return false;

  bool ignore_navigation = false;
  int routing_id = view->GetRoutingID();
  base::string16 url =  request.url().string();
  view->Send (new AwViewHostMsg_ShouldOverrideUrlLoading(routing_id,
                                                         url,
                                                         &ignore_navigation));
  return ignore_navigation;
}

void AwContentRendererClient::RenderViewCreated(
    content::RenderView* render_view) {
  AwRenderViewExt::RenderViewCreated(render_view);

// START: Printing fork b/10190508
  printing::PrintWebViewHelper* print_helper =
      new printing::PrintWebViewHelper(render_view);
  print_helper->SetScriptedPrintBlocked(true);
// END: Printing fork b/10190508
  // TODO(sgurun) do not create a password autofill agent (change
  // autofill agent to store a weakptr).
  autofill::PasswordAutofillAgent* password_autofill_agent =
      new autofill::PasswordAutofillAgent(render_view);
  new autofill::AutofillAgent(render_view, password_autofill_agent);
}

std::string AwContentRendererClient::GetDefaultEncoding() {
  return AwResource::GetDefaultTextEncoding();
}

bool AwContentRendererClient::HasErrorPage(int http_status_code,
                          std::string* error_domain) {
  return http_status_code >= 400;
}

void AwContentRendererClient::GetNavigationErrorStrings(
    WebKit::WebFrame* /* frame */,
    const WebKit::WebURLRequest& failed_request,
    const WebKit::WebURLError& error,
    std::string* error_html,
    string16* error_description) {
  if (error_html) {
    GURL error_url(failed_request.url());
    std::string err = UTF16ToUTF8(error.localizedDescription);
    std::string contents;
    if (err.empty()) {
      contents = AwResource::GetNoDomainPageContent();
    } else {
      contents = AwResource::GetLoadErrorPageContent();
      ReplaceSubstringsAfterOffset(&contents, 0, "%e", err);
    }

    ReplaceSubstringsAfterOffset(&contents, 0, "%s",
                                 error_url.possibly_invalid_spec());
    *error_html = contents;
  }
  if (error_description) {
    if (error.localizedDescription.isEmpty())
      *error_description = ASCIIToUTF16(net::ErrorToString(error.reason));
    else
      *error_description = error.localizedDescription;
  }
}

unsigned long long AwContentRendererClient::VisitedLinkHash(
    const char* canonical_url,
    size_t length) {
  return visited_link_slave_->ComputeURLFingerprint(canonical_url, length);
}

bool AwContentRendererClient::IsLinkVisited(unsigned long long link_hash) {
  return visited_link_slave_->IsVisited(link_hash);
}

}  // namespace android_webview

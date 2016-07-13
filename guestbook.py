#!/usr/bin/env python

# Copyright 2016 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# [START imports]
import os
import urllib

from google.appengine.api import users
from google.appengine.ext import ndb

from google.appengine.ext import ndb
from google.appengine.api import taskqueue

import json, datetime, time

import jinja2
import webapp2

JINJA_ENVIRONMENT = jinja2.Environment(
    loader=jinja2.FileSystemLoader(os.path.dirname(__file__)),
    extensions=['jinja2.ext.autoescape'],
    autoescape=True)
# [END imports]

DEFAULT_GUESTBOOK_NAME = 'default_guestbook'


# We set a parent key on the 'Greetings' to ensure that they are all
# in the same entity group. Queries across the single entity group
# will be consistent. However, the write rate should be limited to
# ~1/second.

def guestbook_key(guestbook_name=DEFAULT_GUESTBOOK_NAME):
    """Constructs a Datastore key for a Guestbook entity.

    We use guestbook_name as the key.
    """
    return ndb.Key('Guestbook', guestbook_name)


# [START greeting]
class Author(ndb.Model):
    """Sub model for representing an author."""
    identity = ndb.StringProperty(indexed=False)
    email = ndb.StringProperty(indexed=False)


class Greeting(ndb.Model):
    """A main model for representing an individual Guestbook entry."""
    author = ndb.StructuredProperty(Author)
    content = ndb.StringProperty(indexed=False)
    date = ndb.DateTimeProperty(auto_now_add=True)
# [END greeting]


# [START main_page]
class MainPage(webapp2.RequestHandler):

    def get(self):
        guestbook_name = self.request.get('guestbook_name',
                                          DEFAULT_GUESTBOOK_NAME)
        greetings_query = Greeting.query(
            ancestor=guestbook_key(guestbook_name)).order(-Greeting.date)
        greetings = greetings_query.fetch(10)



        template_values = {
            'greetings': greetings,
            'guestbook_name': urllib.quote_plus(guestbook_name)
        }

        template = JINJA_ENVIRONMENT.get_template('index.html')
        self.response.write(template.render(template_values))
# [END main_page]


# [START guestbook]
class Guestbook(webapp2.RequestHandler):

    def get(self):
        # We set the same parent key on the 'Greeting' to ensure each
        # Greeting is in the same entity group. Queries across the
        # single entity group will be consistent. However, the write
        # rate to a single entity group should be limited to
        # ~1/second.
        guestbook_name = self.request.get('guestbook_name',
                                          DEFAULT_GUESTBOOK_NAME)
        greeting = Greeting(parent=guestbook_key(guestbook_name))

        greeting.content = self.request.get('content')
        greeting.put()

        query_params = {'guestbook_name': guestbook_name}
        self.redirect('/?' + urllib.urlencode(query_params))
# [END guestbook]

# [START guestbook]
class PullData(webapp2.RequestHandler):

    def get(self):
        # We set the same parent key on the 'Greeting' to ensure each
        # Greeting is in the same entity group. Queries across the
        # single entity group will be consistent. However, the write
        # rate to a single entity group should be limited to
        # ~1/second.
        guestbook_name = self.request.get('guestbook_name',
                                          DEFAULT_GUESTBOOK_NAME)
        ind = int(self.request.get('ind',
                                          0))
        lt = self.request.get('last_time',
                                          datetime.datetime(2000, 1, 1, 0, 00, 00, 100).isoformat())
        lasttime = datetime.datetime.strptime(lt, "%Y-%m-%dT%H:%M:%S.%f")
        greetings_query = Greeting.query(
            ancestor=guestbook_key(guestbook_name)).filter(Greeting.date>lasttime).order(Greeting.date)
        greetings = greetings_query.fetch(100)
        res=[]
        tres=[]
        mxd = lasttime
        for greeting in greetings:
            tim = int(time.mktime(greeting.date.timetuple())) * 1000
            con = greeting.content
            pie = con.split(',')
            if(len(pie)>ind):
                v = pie[ind]
            else:
                v = pie[0]
            try:
                res.append(float(v))
                tres.append(tim)
            except ValueError:
                print "Not a float"
            if greeting.date>mxd:
                mxd=greeting.date
        self.response.headers['Content-Type'] = 'application/json'

        obj = {
            'd': res,
            't': tres,
            'lt':mxd
            }
        date_handler = lambda obj: (
            obj.isoformat()
            if isinstance(obj, datetime.datetime)
            or isinstance(obj, datetime.date)
            else None
        )
        self.response.out.write(json.dumps(obj, default=date_handler))
# [END guestbook]

class DelAll(webapp2.RequestHandler):
    def post(self):
        entries = Greeting.all(keys_only=True)

        bookmark = self.request.get('bookmark')
        if bookmark:
            cursor = ndb.Cursor.from_websafe_string(bookmark)

        query = Greeting.query()
        entries, next_cursor, more = query.fetch_page(
            1000,
            keys_only=True,
            start_cursor=cursor)

        ndb.delete_multi(entries)

        bookmark = None
        if more:
            bookmark = next_cursor.to_websafe_string()

        taskqueue.add(
            url='/task',
            params={'bookmark': bookmark})


# [START app]
app = webapp2.WSGIApplication([
    ('/', MainPage),
    ('/sign', Guestbook),
    ('/del', DelAll),
    ('/data', PullData),
], debug=True)
# [END app]
